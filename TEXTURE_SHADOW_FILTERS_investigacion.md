# Investigación: filtro de texturas y sombras

## Resumen ejecutivo

A diferencia de FSR2/TAA (ver `FSR2_TAA_investigacion.md`), estas dos mejoras
**sí son viables de forma genérica**, porque no dependen de que Cemu entienda la
semántica del renderizado del juego (qué uniform es una matriz, qué buffer es
profundidad de escena) - dependen únicamente de estado que el decompilador y el
creador de samplers de Latte **ya rastrean hoy, por diseño**, para cualquier
juego:

1. **Filtro anisotrópico forzado (global)** - el nivel de anisotropía por
   textura ya existe como campo (`overwriteInfo.anisotropicLevel`), solo falta
   exponer un override global en Ajustes.
2. **PCF más ancho para sombras** - el decompilador ya marca, por texture unit
   y por shader, cuáles fetches son de un sampler de comparación de profundidad
   (shadow map). Ese flag es la única señal que se necesita para inyectar un
   kernel de muestreo más ancho en el punto exacto donde hoy se emite un solo
   `texture(...)`.

Ambas son aditivas, independientes entre sí, y no tocan nada del trabajo de
TAA/FSR1/SMAA ya hecho.

## 1. Filtro anisotrópico forzado

### Qué existe hoy
- Cada `LatteTexture` tiene `overwriteInfo.anisotropicLevel` (`LatteTexture.h:186`,
  `sint8`, `-1` = sin override, `1<<n` si se fija), pero **solo se puede fijar
  por graphic pack**, juego por juego - no hay ninguna opción global de usuario.
- En la creación del sampler de Vulkan (`VulkanRendererCore.cpp:818-832`):
  ```cpp
  auto maxAniso = samplerWords->WORD0.get_MAX_ANISO_RATIO(); // lo que pidió el juego
  if (baseTexture->overwriteInfo.anisotropicLevel >= 0)
      maxAniso = baseTexture->overwriteInfo.anisotropicLevel; // override del graphic pack
  if (maxAniso > 0) { samplerInfo.anisotropyEnable = VK_TRUE; samplerInfo.maxAnisotropy = (float)(1 << maxAniso); }
  ```
  El mismo patrón existe en el path de OpenGL y en `MetalSamplerCache.cpp`.

### Qué falta
- Un `ConfigValue<sint32> force_anisotropic_level` en `CemuConfig.h` (Apagado /
  2x / 4x / 8x / 16x), con su dropdown en `GeneralSettings2.cpp` (mismo patrón
  que `smaa_quality` o el nuevo `taa_spatial_aa` que ya agregamos).
- En cada uno de los 3 sitios de creación de sampler (Vulkan/OpenGL/Metal),
  aplicar el override global **solo si el graphic pack no fijó ya un valor
  propio** (`overwriteInfo.anisotropicLevel < 0`), para no romper ajustes finos
  que algunos graphic packs ya hacen a propósito.

### Cuidado (lección de Dolphin)
Dolphin tuvo que aprender esto por las malas: forzar anisotropía sobre una
textura que el juego pidió con filtro `nearest`/point (arte pixelado a
propósito) se ve mal - la anisotropía solo tiene sentido combinada con filtro
lineal. Su fix fue "no usar anisotropía sobre texturas con point filtering"
(`dolphin-emu@83d3055`). Nuestra implementación debe replicar esa misma regla:
si el `MAG_FILTER`/`MIN_FILTER` que pidió el juego es `POINT`, no aplicar el
override de anisotropía salvo que el usuario también fuerce filtro lineal.

Referencias: [Dolphin PR #11276 - Force linear filtering](https://github.com/dolphin-emu/dolphin/pull/11276),
[Dolphin PR #13368 - Game-requested anisotropic filtering](https://github.com/dolphin-emu/dolphin/pull/13368),
[Anisotropic filtering - Wikipedia](https://en.wikipedia.org/wiki/Anisotropic_filtering).

## 2. PCF más ancho para sombras

### Qué existe hoy
- El decompilador marca por texture-unit si es un sampler de comparación de
  profundidad: `shader->textureUsesDepthCompare[i]` (seteado durante el
  análisis, consumido en `LatteDecompilerEmitGLSLHeader.hpp:290` para emitir
  `sampler2DShadow` en vez de `sampler2D`, y en el header de MSL,
  `LatteDecompilerEmitMSLHeader.hpp:472`, para emitir un `depth2d` de Metal).
  Esto es **estructural y genérico** - no depende de qué juego sea.
- La emisión real del fetch para instrucciones de comparación (`GPU7_TEX_INST_SAMPLE_C`,
  `_C_L`, `_C_LZ`) vive en `LatteDecompilerEmitGLSL.cpp:2389-2411` - hoy emite
  un solo `texture(` / `textureLod(` / `textureOffset(`, que Vulkan resuelve
  como una única comparación bilineal (PCF 2x2 "gratis" de hardware,
  `VulkanRendererCore.cpp:836-857`, vía `samplerInfo.compareEnable` +
  `compareOp`). Eso suaviza apenas el borde - no da penumbra real.

### Qué falta
- En el mismo punto (`LatteDecompilerEmitGLSL.cpp:2389-2411`), cuando el
  opcode es una variante `_C*` **y** la opción de filtro de sombra está
  activada, emitir una llamada a una función helper generada una vez por
  shader (ej. `pcfSample3x3(samplerX, uv, compareZ)`) en vez del `texture(`
  directo. El helper hace un grid fijo (3x3 o 5x5, configurable) de muestras
  con offset de un texel, usando `textureSize(samplerX, 0)` para el paso, y
  promedia el resultado - la técnica estándar de PCF con kernel ancho.
- Nueva opción en `CemuConfig.h` (ej. `shadow_pcf_quality`: Nativo / 3x3 / 5x5)
  y su dropdown en Ajustes.
- Esto es shader codegen genérico (toca el decompilador, no un juego
  puntual), así que el radio de prueba debe cubrir varios juegos con sombras
  duras conocidas, no solo BOTW/Splatoon, para confirmar que no rompe ningún
  shader con la variante `_C_L`/`_C_LZ` (LOD explícito) o con `hasOffset` ya
  activo.

### Por qué esto SÍ es genérico (a diferencia de TAA)
El bloqueo de FSR2/TAA era que Cemu no sabe qué uniform es una matriz de
cámara - eso es semántica del juego, no hay hook de motor. Acá no hay ese
problema: `textureUsesDepthCompare[i]` ya es una propiedad que el decompilador
calcula hoy, para todo shader, de forma 100% genérica, precisamente porque es
una propiedad de la instrucción GPU7 (`SAMPLE_C*`) y no algo que dependa de
interpretar la lógica del juego.

Referencias: [PCF - Fabien Sanglard](https://fabiensanglard.net/shadowmappingPCF/),
[Tutorial 42 - Percentage Closer Filtering, OGLDev](https://ogldev.org/www/tutorial42/tutorial42.html),
[Percentage Closer Filtering issue - Bevy engine](https://github.com/bevyengine/bevy/issues/3628).

## Veredicto

Ambas mejoras son de bajo riesgo arquitectónico y no compiten con el trabajo de
TAA/FSR1/SMAA. Orden de implementación sugerido:

1. **Filtro anisotrópico forzado** primero - cambio pequeño y acotado (3 sitios
   de creación de sampler + 1 config + 1 dropdown), riesgo mínimo, beneficio
   inmediato y visible.
2. **PCF ancho para sombras** después - toca el decompilador (superficie más
   amplia), necesita más testing entre juegos, pero es la mejora con mayor
   impacto visual real (sombras duras/pixeladas es una queja común en juegos de
   Wii U corridos a resoluciones más altas que la nativa).

No se recomienda PCSS (penumbra variable según distancia luz-oclusor) como
primer paso - necesita un segundo render pass para buscar bloqueadores
(blocker search), bastante más trabajo que un PCF de kernel fijo, y el
kernel fijo ya es una mejora sustancial sobre el 2x2 actual.
