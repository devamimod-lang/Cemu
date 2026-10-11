# Investigación: nitidez de FSR1 y TAA entre 720p y 1440p

Objetivo: que FSR1 rinda al máximo de 720p a 1440p, y que el TAA deje de
emborronar sin perder el antialiasing.

Estado de partida: TAA restaurado y funcionando (`a34ed90`), SSAO correcto, sin
jitter, sin reproyección.

---

## 1. La causa del borrón del TAA: el FXAA corre antes de escalar

Esto no es un problema de ajuste, es el orden de los pases. Comparando las dos
rutas en `VulkanRenderer.cpp`:

| Paso | Sin TAA (`DrawBackbufferQuadTwoPass`) | Con TAA (`DrawBackbufferQuadFsr1Taa`) |
|---|---|---|
| 1 | EASU: upscale 720p → 1440p | **FXAA a 720p** (pre-pase espacial) |
| 2 | RCAS | Resolve temporal (nativo) |
| 3 | **FXAA a 1440p** | EASU: upscale 720p → 1440p |
| 4 | — | RCAS |

En modo TAA el FXAA se aplica a resolución nativa y **su suavizado se amplía 2×
al escalar**. En modo FXAA se aplica ya a resolución de salida, donde su huella
es de un píxel de pantalla. De ahí que el borrón aparezca *sólo* con TAA activo,
exactamente como se reportó.

### Lo que descarté, y por qué

Mi primera sospecha fue que el resolve acumulara suavizado por el remuestreo
bicúbico del historial. **Es falsa, y se puede demostrar.** En
`SampleHistoryBicubic`, con la imagen quieta y alineada a téxel, `st =
uv*texSize - 0.5` es entero, así que `f = 0`, y los pesos salen `w0=0, w1=1,
w2=0, w3=0`, `s0=1`, `f0=0`, `m0=base`. Los cinco tap colapsan a `C`, el téxel
exacto: **el pase es una identidad**, no suaviza nada.

Y está alineado de verdad: `textureSrcResolution` se llena con
`GetEffectiveSize` del textura del juego, y los targets de resolve e historial
se crean con ese mismo `nativeWidth/nativeHeight`. No hay desalineación.

Tampoco es el peso de mezcla: con la imagen quieta `ghost ≈ 0` y `blendCur =
0.10`, pero mezclar dos imágenes idénticas devuelve esa misma imagen.

El pre-pase no se puede simplemente mover después del upscale: sin jitter, el
resolve no puede antialiasar por sí mismo, así que **todo** el AA viene del
pre-pase, y FSR1 exige entrada antialiaseada (ver §2).

---

## 2. Requisitos de FSR1 que AMD documenta, contra lo que hacemos

De la guía oficial de integración:

| Requisito | ¿Lo cumplimos? |
|---|---|
| Entrada bien antialiaseada (TAA, MSAA…) | **Sí** — pre-pase + resolve |
| Normalizada a [0,1] | **Sí** — los targets son UNORM de 8 bits |
| Espacio de color perceptual (sRGB, no lineal) | **Sí** — el epílogo de gamma es identidad en los pases intermedios (`targetGamma = displayGamma`), y la salida del juego ya es gamma |
| Sin ruido | **Sí** |
| **MIP bias negativo para aumentar el detalle de textura** | **NO** |
| Después del tonemapping, antes de grano/UI | **Sí** |

### El MIP bias es el hueco grande

AMD lo lista como requisito, no como opción. La fórmula es:

```
mipBias = -log2(displayResolution / renderResolution)
```

| Escenario | Factor | mipBias |
|---|---|---|
| 720p → 1440p | 2.00× | **−1.000** |
| 720p → 1080p | 1.50× | −0.585 |
| 1080p → 1440p | 1.33× | −0.415 |

Sin esto, los mips se seleccionan para 720p: tras un upscale 2× **no hay
frecuencia alta de textura que EASU pueda reconstruir**. Es, literalmente, la
razón de que "no se note mucho". Y el survey de TAA apunta a la misma causa por
el otro lado: *"las texturas ya se han difuminado durante el mipmapping"* es una
de las dos fuentes documentadas de borrón del TAA. Un solo cambio ataca las dos
quejas.

**Cemu ya tiene la infraestructura**: `LatteTexture.h` define
`overwriteInfo.lodBias` y `relativeLodBias` en pasos de 1/64, pero sólo las
escriben las reglas de graphic pack (`LatteTexture.cpp:1305`). **No hay ajuste
global.** Y existe el precedente exacto de cómo añadirlo:
`force_anisotropic_level`, que ya respeta la regla de que el override del
graphic pack gana.

---

## 3. Lo que ya está bien y no hay que tocar

- **El remuestreo del historial es Catmull-Rom** (`float s = 0.5`). La
  literatura lo señala como la elección correcta precisamente para evitar
  acumular borrón, y además es *ligeramente* realzante. Ya está bien.
- **El orden arquitectónico es correcto**: AA antes del upscale, FSR1 sobre
  imagen antialiaseada, RCAS al final a resolución de salida. Coincide con la
  guía de AMD.
- **RCAS está en el sitio correcto**: a resolución de display, así que su
  realce no se amplía.

---

## 4. Una trampa concreta que hay que anotar

AMD advierte: **"una entrada negativa a RCAS devolverá NaN"**.

El plan anterior tenía una "fase H: subir `EnsureFsr1EasuIntermediateTarget` a
16F". **Hay que descartarla.** Ese target es hoy UNORM de 8 bits, que satura y
por tanto nunca entrega negativos a RCAS. Subirlo a flotante quitaría esa
saturación y expondría exactamente el fallo que AMD describe — es el mismo tipo
de error que produjo la pantalla negra del TAA al restaurar el shader de 8 bits
sobre un historial de 16F.

---

## 5. Plan propuesto, por relación impacto/riesgo

### A. Pre-pase espacial de FXAA a SMAA *(coste cero, ya implementado)*
`taa_spatial_aa` ya ofrece `kTaaSpatialSmaa`, pero el valor por defecto es
`kTaaSpatialFxaa`. SMAA sólo mezcla a lo largo de bordes detectados, mientras
FXAA aplica un desenfoque dirigido por luma de forma amplia. Amplificada 2×, la
diferencia debería ser grande. **Es un cambio de ajuste, sin compilar: la prueba
A/B más barata que existe aquí, y hay que hacerla antes que nada.**

### B. MIP bias global negativo, automático según el factor de escala
Un ajuste `force_lod_bias` siguiendo el patrón de `force_anisotropic_level`,
con una opción "Auto (FSR1)" que calcule `-log2(display/render)`. Es el
requisito que falta de FSR1 y ataca las dos quejas a la vez.

Riesgo conocido: un bias demasiado negativo reintroduce aliasing de textura y
shimmer en superficies en ángulo. Mitigante: la anisotropía forzada ya existe en
esta build, y es justo lo que compensa eso. Por eso conviene que el ajuste sea
un slider con "Auto", no un valor fijo.

### C. RCAS de 0.20 a 0.00 stops
La escala de AMD va de 0.0 (máxima nitidez) y **cada número entero divide la
nitidez por dos**. El valor por defecto nuestro es 0.20. Hay margen, es un
ajuste, y conviene medirlo *después* de B para no confundir las dos causas.

### D. Sólo si A–C no bastan
Un realce extra a resolución nativa, después del resolve y antes del EASU.
Dejarlo para el final a propósito: añadir un afilador para compensar un borrón
cuya causa estructural (§1) sigue ahí es tratar el síntoma, y es el patrón que
ya dio problemas en este proyecto.

---

## 6. Qué está verificado y qué no

**Verificado en el código de esta build:**
- El orden de pases de las dos rutas (§1), leído en `VulkanRenderer.cpp`.
- Que `SampleHistoryBicubic` es identidad en téxeles alineados (§1), derivado
  de los pesos.
- Que `textureSrcResolution` y los targets usan el mismo tamaño nativo.
- Que no existe LOD bias global, y que la infraestructura por-textura sí.
- Que el pre-pase por defecto es FXAA y RCAS por defecto 0.20 stops.
- Que el intermedio de EASU es UNORM de 8 bits.

**No verificado, pendiente de medir:**
- Cuánto del borrón aporta el FXAA a 720p frente al upscale en sí. La prueba A
  lo separa.
- Si el MIP bias de −1.0 es demasiado agresivo en BotW con la anisotropía
  forzada activa.

---

## Fuentes

- [AMD FidelityFX Super Resolution 1 (FSR 1) — GPUOpen](https://gpuopen.com/fidelityfx-superresolution/)
- [FidelityFX Super Resolution 1.2 (FSR1) — manual del SDK](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-spatial/)
- [AMD FSR 1.0 demystified — jntesteves](https://jntesteves.pages.dev/posts/amd-fsr-demystified/)
- [How to use AMD FSR 1.0 Unreal Engine 4 Plugin — GPUOpen](https://gpuopen.com/learn/ue4-fsr/)
- [A Survey of Temporal Antialiasing Techniques — Yang, Liu, Salvi](http://behindthepixels.io/assets/files/TemporalAA.pdf)
- [Temporal AA and the Quest for the Holy Trail — elopezr](https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/)
- [Temporal Antialiasing Starter Pack — Alex Tardif](https://alextardif.com/TAA.html)
- [Bilinear texture filtering: artifacts, alternatives — Bart Wronski](https://bartwronski.com/2020/04/14/bilinear-texture-filtering-artifacts-alternatives-and-frequency-domain-analysis/)
- [Unity + AMD FSR, SIGGRAPH 2021](https://advances.realtimerendering.com/s2021/Unity%20AMD%20FSR%20-%20SIGGRAPH%202021.pdf)

---

## 7. CORRECCIÓN: la configuración real desmonta parte de lo anterior

Lo de arriba se escribió leyendo los valores **por defecto del código** y
presentándolos como el estado real de la máquina. Eso fue un error de método: el
`settings.xml` estaba a un comando de distancia y no se abrió. Los valores
reales:

| Ajuste | Valor real | Lo que había afirmado |
|---|---|---|
| `TaaSpatialAA` | **1 = SMAA** | "por defecto FXAA; cambiarlo a SMAA" |
| `FsrRcasSharpness` | **200 = 2.00 stops, el más SUAVE** | "por defecto 0.20 stops" |
| `FsrRcasDenoise` | **true** | no comprobado |
| `ForceAnisotropicLevel` | **0 = apagada** | no comprobado |
| `AntialiasingMode` | 3 = TAA | correcto |
| `UpscaleFilter` | 4 = FSR1 | correcto |

### Consecuencias

1. **La recomendación A (FXAA → SMAA) ya estaba aplicada.** No aporta nada.
2. **La causa principal de "FSR1 no se nota" NO es el MIP bias.** Es que RCAS
   está en su valor más suave. En la escala de AMD cada número entero divide la
   nitidez por dos, así que 2.00 stops es **un cuarto** de la nitidez máxima, y
   con el denoise activo encima. El afilador está prácticamente apagado.
3. **El MIP bias sigue siendo un hueco real y documentado**, pero pasa a ser la
   mejora fina posterior, no la explicación primaria. Se mide después.
4. El análisis de §1 (el FXAA/SMAA del pre-pase corre a resolución nativa y su
   suavizado se amplía al escalar) **sigue en pie**: es estructural y se leyó del
   código, no de los valores por defecto. Pero con SMAA ya activo, su
   contribución es menor de lo que sería con FXAA.

### Por qué el ajuste estaba en el extremo equivocado: un defecto de la GUI

El slider de FSR sharpness se dibuja invertido para que "lo más nítido quede a
la derecha", pero la cifra junto a él mostraba el valor **almacenado** de AMD.
Resultado: marcaba `2.00` en el extremo suave y `0.00` en el nítido, es decir
**el número bajaba al arrastrar hacia "más nítido"**, contradiciendo al tooltip
del propio control.

Corregido: la etiqueta es ahora un porcentaje de nitidez que sube con el slider
(derecha = 100%). Y la ruta de recarga de ajustes refresca esa etiqueta, que
antes era una variable local capturada sólo por el handler del slider, así que
al reabrir ajustes el número quedaba con el valor de la configuración anterior.

### Orden de medición correcto

1. Subir FSR sharpness al máximo y desmarcar el grain-aware sharpening.
2. Anisotropía forzada a 16x.
3. **Sólo entonces** evaluar el MIP bias, para no atribuirle una mejora que
   venía de (1).
