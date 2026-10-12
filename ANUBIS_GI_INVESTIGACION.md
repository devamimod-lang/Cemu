# Investigación: GI y denoising con licencia permisiva

Objetivo: mejorar el GI (espacio de pantalla o ray tracing) con código de
licencia compatible, y añadir denoising.

Restricción dura del proyecto, que decide todo lo demás: **un emulador no es el
motor**. Lo único disponible por píxel es **profundidad y color**. No hay
normales reales, ni materiales, ni albedo separado, ni vectores de movimiento, ni
geometría con transformaciones para construir un BVH.

Licencia del proyecto: **MPL-2.0**. Compatible con MIT y Apache-2.0.
Incompatible con GPL y con CC BY-NC (ya se descartó `ReshadeMotionEstimation` y
TFAA por esto mismo).

---

## 1. Descartado: NVIDIA NRD

**Corrección a lo que afirmé antes.** Dije que NRD era MIT. **No lo es**: lleva
licencia propia de NVIDIA, no permisiva.

Y la licencia no es ni el problema principal. Sus entradas obligatorias:

| Entrada | ¿La tenemos? |
|---|---|
| `IN_MV` — vectores de movimiento, **obligatorios** | **No** |
| `IN_NORMAL_ROUGHNESS` — normal en espacio de **mundo** + roughness lineal | **No** |
| `IN_VIEWZ` | Sí |

Nuestras normales se **reconstruyen derivando la profundidad** y están en
espacio de vista, no de mundo. Roughness no existe: requiere información de
material. Y los vectores de movimiento son justo lo que las fases C/D no
lograron producir.

Doblemente bloqueado. Fuera.

## 2. Descartado: RTXGI / cualquier GI con ray tracing

Requiere estructuras de aceleración (BVH) construidas de la geometría de la
escena, más normales y albedo. Cemu ve un flujo de comandos de GPU: búferes de
vértices en formatos del juego, shaders decompilados de microcódigo Latte y un
framebuffer. No hay escena identificable.

Es el mismo muro, por la misma razón, que ya tumbó dos cosas en este proyecto:
el SSR (*"raymarching sin información de material significa que toda superficie
refleja"*) y la fase D del TAA (las matrices del juego viven en uniform buffers
con layout desconocido).

**No es cuestión de dificultad: falta la entrada que el algoritmo necesita.**

---

## 3. Lo que SÍ es usable

### 3.1 Raymarching sobre buffer de profundidad — Tomasz Stachowiak *(la mejor pieza)*

**Licencia: dual Apache-2.0 / MIT.** Compatible.

Del autor de `kajiya`. Es exactamente lo que nos encaja: **sólo necesita el
buffer de profundidad**, nada de normales ni materiales.

Y resuelve un defecto real que nuestro GI actual tiene. El shader nuestro marcha
a pasos fijos muestreando con `texture()` a secas:

```glsl
const int FARO_GI_STEPS = 8;
float t = (float(s) - 0.5) / float(FARO_GI_STEPS);
vec2 suv = passUV + dirPx * (t * marchPx) / outRes;
```

Eso sufre el problema de la naturaleza discreta del buffer de profundidad: o
produce oclusiones falsas por cuantización (el efecto "ladrillo de Duplo"), o
artefactos de "film plástico" sobre las superficies. La técnica de Stachowiak lo
mitiga usando **muestreo puntual y bilineal simultáneamente**, y añade
**refinamiento por bisección y método de la secante**, que es precisamente lo
que hace falta cuando el marchado se usa para **tomar color** (nuestro caso) y
no sólo para decidir visibilidad.

**Aplicabilidad: directa.** Es un reemplazo del bucle de marchado dentro de
`s_gi_shader_body`, sin tocar nada más.

### 3.2 SSGI de Martins Upitis (Godot) — MIT

Implementación sencilla de SSGI, portada después a Unity URP. Útil como
referencia comparativa, no como reemplazo: el propio autor la describe como no
perfecta.

### 3.3 SSIL de Godot — MIT (Godot entero lo es)

Importante por lo que revela: la SSIL de Godot pertenece a la **misma familia**
que nuestro GI actual — muestreo basado en horizonte, derivado del linaje
ASSAO/XeGTAO, recogiendo color en lugar de oclusión.

O sea: **nuestro enfoque ya es el correcto**. No hay que reemplazarlo, hay que
mejorarlo.

### 3.4 Visibility bitmask (Therrien et al., 2023) — pendiente de verificar licencia

Esquema de muestreo por horizonte que corrige la **sobre-oclusión** típica de
los métodos tipo GTAO, representando la visibilidad como una máscara de bits en
lugar de un único ángulo máximo. Nuestro shader usa exactamente ese `maxSin`
único:

```glsl
maxSin = max(maxSin, sinAngle);
```

Es una mejora algorítmica real sobre lo que tenemos. Hay que comprobar la
licencia de las implementaciones antes de mirar código.

---

## 4. El problema de fondo: denoising sin vectores de movimiento

Todo GI estocástico es ruidoso por diseño — por eso XeGTAO y cualquier
SSAO/SSGI real exige un pase de denoise. Hoy lo limpiamos con un blur bilateral
7×7 de una sola pasada, que es tosco.

Las dos vías de mejora:

**a) Denoise espacial mejor.** Viable ya. El MXAO de ReShade usa **dos** pases
bilaterales en lugar de uno, y la arquitectura de 5 pases de `qUINT_mxao.fx` ya
está estudiada en `FARO_TAA_FSR1_DEPTH_investigacion.md`. Mejora acotada y sin
dependencias nuevas.

**b) Acumulación temporal.** Es lo que de verdad elimina el ruido, y **necesita
reproyección**, o sea vectores de movimiento. Eso nos devuelve exactamente al
muro de las fases C/D.

Hay que ser honesto sobre esto: **sin vectores de movimiento, el techo de
calidad del GI está puesto**. Se puede subir bastante con (3.1) y (a), pero la
estabilidad temporal que da el aspecto "limpio" de un GI moderno no se alcanza.

---

## 5. Plan recomendado, por relación valor/riesgo

1. **Sustituir el bucle de marchado por la técnica de Stachowiak** (3.1).
   Licencia compatible, sólo necesita profundidad, y ataca un defecto concreto y
   demostrable del shader actual. Es un cambio contenido a un solo shader.
2. **Segundo pase bilateral en el denoise** (4a). Contenido, sin dependencias.
3. **Evaluar visibility bitmask** (3.4), empezando por su licencia.
4. **No** intentar NRD ni RTXGI. No es dificultad, es que falta la entrada.

### Antes de nada

GI aún no se ha visto funcionando una sola vez: el punto de inserción sólo
existe en la cadena TAA sin FSR1, y la configuración activa usa FSR 1.0. Hay que
verlo con un filtro no-FSR1 antes de invertir en mejorarlo — mejorar algo que
nadie ha visto todavía es construir sobre una suposición.

---

## Fuentes

- [NVIDIA-RTX/NRD — GitHub](https://github.com/NVIDIA-RTX/NRD)
- [Depth buffer raymarching for contact shadows, SSGI, SSR — Tomasz Stachowiak (gist, Apache-2.0/MIT)](https://gist.github.com/h3r2tic/9c8356bdaefbe80b1a22ae0aaee192db)
- [RTX Global Illumination — NVIDIA Developer](https://developer.nvidia.com/rtx/ray-tracing/rtxgi)
- [NVIDIA-RTX/RTXGI — GitHub](https://github.com/NVIDIA-RTX/RTXGI)
- [SSGI-URP (port del SSGI MIT de Martins Upitis) — GitHub](https://github.com/demonixis/SSGI-URP)
- [Replace SSIL with SSGI (Godot) — pull request](https://github.com/NeoCry/godot_a/pull/70)
- [SDFGI sobre MiniEngine — GitHub](https://github.com/carlos-lopez-garces/SDFGI)
