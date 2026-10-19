# Plan: escalado de texturas con detalle real

Objetivo: que el escalado de texturas **se note**, revelando detalle en texturas
que el juego trae aplanadas o pixeladas. El filtro actual (Catmull-Rom en GPU)
funciona y no produce artefactos, pero su techo es bajo por construcción.

---

## 0. La medición que cambia el diagnóstico

Antes de cambiar de técnica, lo que dicen los contadores con escalado a 4×:

```
733 copies built, 3981 declined for budget, 1023 MB of copies held, 0 queued
```

**Solo el 15% de las texturas candidatas tiene copia escalada.** Dos causas que
se suman:

1. **A 4× una copia es enorme.** Una textura de 1024² escalada 4× son 4096² en
   RGBA8 = **64 MB**. Un presupuesto de 1 GB cabe ~16 texturas grandes.
2. **No hay expulsión LRU.** El presupuesto se llena por orden de llegada, con
   lo que cargó primero (menús, assets iniciales), y las texturas que el
   jugador está mirando se rechazan indefinidamente.

**Consecuencia para la evaluación:** la impresión de "no veo cambio" se midió
sobre un escenario donde la mayoría de superficies visibles **no estaban
escaladas**. No se puede juzgar el filtro en esas condiciones. Eso no invalida
la conclusión —el techo de un filtro es real— pero sí obliga a arreglar esto
antes de atribuir nada a la calidad del filtro.

El presupuesto fijo de 1024 MB fue un error de diseño: el valor correcto depende
de la VRAM del dispositivo, no de un número elegido a mano.

---

## Fase 1 — Desbloquear la evaluación

### 1.1 Presupuesto derivado de la VRAM real

Sustituir el número fijo por un cálculo. Cemu ya consulta los heaps de memoria
en `VKRMemoryManager` (`vkGetPhysicalDeviceMemoryProperties`, sumando los heaps
`DEVICE_LOCAL`), así que la información está disponible sin añadir nada.

- Modo **Auto** por defecto: una fracción de la VRAM libre del dispositivo.
- Override manual para quien quiera forzarlo.
- `VK_EXT_memory_budget` da el uso y presupuesto reales por heap y sería más
  preciso que el tamaño del heap; comprobar si el driver lo expone.

El presupuesto **no puede eliminarse**: al agotar la VRAM el driver empieza a
volcar a RAM del sistema por PCIe y el rendimiento se hunde. Lo que se corrige
es que el límite lo fije el hardware, no una constante.

### 1.2 Expulsión LRU

Marcar cada copia con el último frame en que se usó y, al no caber una nueva,
liberar la menos reciente. Sin esto el presupuesto es "lo primero que llegó" en
lugar de "lo que se está viendo", que es el fallo observado.

Nota de implementación: las copias son `VKRObjectTexture`/`VKRObjectTextureView`
propiedad de cada `LatteTextureVk`, así que la expulsión necesita un índice
global de qué texturas tienen copia. Liberar debe pasar por
`ReleaseDestructibleObject` con `flagForCurrentCommandBuffer()`, nunca destruir
en el punto de uso.

---

## Fase 2 — Medir el filtro actual en condiciones justas

Con el 80% de superficies visibles escaladas, comparar Off / 2× / 4× en caliente
(ya es posible sin reiniciar). Esto da la línea base contra la que medir
cualquier técnica nueva, y evita atribuir a un algoritmo lo que era un problema
de presupuesto.

**Predicción explícita, para que quede comprobable:** la mejora seguirá siendo
modesta. Catmull-Rom sobre 4×4 téxeles es una reconstrucción más nítida que el
tent bilineal 2×2 del hardware, pero **no inventa detalle que no esté en el
archivo**. En una textura aplanada no hay alta frecuencia que reconstruir.

---

## Fase 3 — Escaladores que generan detalle

Un filtro interpola; una red convolucional **sintetiza** lo que no está. Esa es
la diferencia que hace falta aquí.

El diseño es un **escalador seleccionable**, no uno impuesto: la calidad depende
del contenido y del hardware, así que la elección es del usuario.

### Licencias verificadas (vía API de GitHub, octubre 2026)

| Implementación | Licencia | Usable en MPL-2.0 |
|---|---|---|
| bloc97/Anime4K | **MIT** | ✅ |
| Saafke/FSRCNN_Tensorflow | **Apache-2.0** | ✅ — incluye pesos `.pb` |
| Lornatang/FSRCNN-PyTorch | **Apache-2.0** | ✅ |
| Nhat-Thanh/FSRCNN-Pytorch | **MIT** | ✅ |
| igv/FSRCNN-TensorFlow | GPL-3.0 | ❌ |
| yjn870/FSRCNN-pytorch | **sin licencia** | ❌ sin licencia = todos los derechos reservados |
| xBRZ | GPL-3.0 | ❌ |

Lo que se necesita no es el framework de entrenamiento sino **los pesos
preentrenados**: la arquitectura viene de un paper publicado y no es el artefacto
con licencia. Por eso Saafke (Apache-2.0, con `.pb` para `dnn_superres` de
OpenCV) es la vía más directa para FSRCNN.

### 3.1 Anime4K Restore CNN (MIT) — *empezar por aquí*

Estructura real del shader `Anime4K_Restore_CNN_S.glsl`, ya verificada:

- **4 pases**, 3 texturas intermedias (`conv2d_tf`, `conv2d_1_tf`, `conv2d_2_tf`)
- Convoluciones 3×3 con mapas de 4 y 8 canales
- **Los pesos van incrustados en el GLSL como constantes `mat4`** — no hay que
  cargar modelos externos ni añadir dependencias
- Variantes S (17 KB) → M → L → VL → UL (309 KB), complejidad creciente
- Cabecera MIT dentro del propio archivo

Por qué primero: es el camino más corto a un resultado visible. No requiere
infraestructura de inferencia, y el pipeline multipase render-to-texture **ya
existe** en esta build (es lo que construimos para el escalado por GPU). Portar
las directivas de mpv (`//!HOOK`, `//!BIND`, `//!SAVE`, `//!WIDTH/HEIGHT`,
`//!COMPONENTS`) a nuestros pases es trabajo mecánico, no diseño nuevo.

**Riesgo principal, dicho por delante:** Anime4K está entrenada con **vídeo de
anime**, para deshacer artefactos de compresión y desenfoque de escalado. No
está entrenada con texturas de juego magnificadas 8×. El arte cel-shaded de BotW
está más cerca de su dominio que contenido fotográfico, pero sigue siendo una
discrepancia de dominio y puede producir resultados raros en materiales
concretos. Hay que probarlo antes de celebrarlo.

### 3.2 FSRCNN (Apache-2.0)

Red diseñada explícitamente para superresolución: extracción de características
→ shrinking → mapping no lineal → expanding → deconvolución. 4–8 capas
convolucionales y menos de 10K parámetros, concebida para tiempo real.

Ventaja sobre Anime4K para este uso: **está entrenada para escalar**, no para
restaurar vídeo. Es la tarea que de verdad queremos.

Trabajo extra que implica: los pesos vienen en `.pb` (protobuf de TensorFlow) y
hay que convertirlos a constantes de shader, al estilo de lo que hace Anime4K.
Es mecánico pero es un paso que Anime4K no necesita — de ahí el orden.

### 3.3 Escaladores dirigidos por bordes

SABR y similares (origen libretro) producen bordes limpios en lugar de
interpolación suave, y son baratos. Útiles para texturas de tipo pixel-art o UI,
donde una CNN entrenada en fotografía o anime puede comportarse peor que un
algoritmo que simplemente respeta los bordes.

**xBRZ queda descartado**: GPL-3.0, incompatible con este proyecto.

---

## Reglas que cualquier escalador nuevo hereda

Estas no son negociables y vienen de fallos ya pagados en este proyecto:

1. **El caché de texturas queda 1:1 con la memoria del juego.** Nada de
   sobrescribir dimensiones de la textura host — ese camino corrompió el frame y
   está documentado en el commit `b65f534`.
2. **Solo 2D de una capa.** Arrays y cubemaps necesitan una copia por capa y una
   vista del tipo correcto; sin eso, una copia de la capa 0 se sustituye para
   todas y aparecen texturas de otras partes.
3. **Nunca texturas de profundidad.** Se leen como datos; escalarlas rompe SSAO
   y GI.
4. **Mínimo de 128 px de lado.** En un volcado de BotW, 1295 de 3975 texturas
   base eran ≤64²: LUTs, rampas, máscaras, UI. Interpolar una LUT produce
   valores que la tabla no contiene — corrompe el render.
5. **BC4/BC5 fuera.** Formatos de 1 y 2 canales para máscaras y normal maps;
   pasarlos por una ruta RGB corrompe el significado de los canales.
6. **Creación y grabación solo en el límite de frame**, y destrucción solo vía
   `ReleaseDestructibleObject` tras `flagForCurrentCommandBuffer()`. Un límite
   de frame no es garantía de que el command buffer terminó.
7. **Fallo suave siempre.** Cualquier problema devuelve la vista original y el
   frame sale idéntico a uno sin la feature.
8. **Contadores conectados al log desde el primer commit.** Tres veces en este
   trabajo se escribieron contadores que nada leía, y cada vez costó una ronda
   de depuración a ciegas.

---

## Lo que está verificado y lo que no

**Verificado:**
- Los contadores de saturación del presupuesto (733 / 3981 / 1023 MB).
- Las licencias de la tabla, consultadas vía API de GitHub.
- La estructura de `Anime4K_Restore_CNN_S.glsl`: 4 pases, 3 intermedias, pesos
  `mat4` inline, cabecera MIT.
- Que Cemu ya consulta los heaps de memoria del dispositivo.
- Que el pipeline multipase render-to-texture existe en esta build.

**No verificado, y hay que medirlo:**
- Cuánta mejora real aporta Catmull-Rom con el presupuesto arreglado. Es la
  línea base y aún no se tiene.
- Si Anime4K, entrenada en anime, aporta algo en texturas de juego. Es el riesgo
  central de la fase 3.1.
- El coste de 4 pases de CNN por textura sobre cientos de texturas al cargar una
  zona. Puede exigir escalonar el trabajo entre frames.
- Si `VK_EXT_memory_budget` está disponible en el driver.

---

## Fuentes

- [FSRCNN — Accelerating the Super-Resolution CNN (paper)](https://arxiv.org/pdf/1608.00367)
- [bloc97/Anime4K](https://github.com/bloc97/Anime4K)
- [Saafke/FSRCNN_Tensorflow](https://github.com/Saafke/FSRCNN_Tensorflow)
- [Lornatang/FSRCNN-PyTorch](https://github.com/Lornatang/FSRCNN-PyTorch)
- [Nhat-Thanh/FSRCNN-Pytorch](https://github.com/Nhat-Thanh/FSRCNN-Pytorch)
- [A Deep Journey into Super-resolution: A survey](https://arxiv.org/pdf/1904.07523)
