# Investigación: efectos estilo ReShade nativos (sin instalar ReShade)

## Resumen ejecutivo

La pregunta tiene dos mitades con viabilidad **muy distinta**, y es clave no
mezclarlas:

1. **Efectos de post-proceso que solo necesitan la imagen final a color**
   (sharpening/CAS, corrección de color/LUTs, viñeta, aberración cromática,
   grano de película, scanlines/CRT) - **totalmente viables hoy**, reusando
   *exactamente* el mismo punto de enganche que ya construimos para
   FXAA/SMAA/TAA/FSR1 (`RendererOutputShader` + `DrawBackbufferQuad*` en
   `VulkanRenderer.cpp`). Esto cubre, en la práctica, los efectos de ReShade
   que la gente más usa (CAS sharpening y corrección de color son los top-2
   en casi cualquier ranking de presets populares).
2. **"Sombras reales"/AO por screen-space** (SSAO, contact-hardening
   shadows, screen-space shadows, depth of field) - necesitan el **depth
   buffer de la escena en el momento del composite final**, y ese dato *ya
   se perdió* para cuando llegamos a esa etapa - **el mismo bloqueo
   arquitectónico que documentamos en `FSR2_TAA_investigacion.md`** (ahí era
   depth + vectores de movimiento; acá sería "solo" depth, pero con el mismo
   problema de fondo: Cemu no sabe *cuál* de los N render targets de
   profundidad que el juego usó este frame es "la" profundidad de escena).

No recomiendo intentar replicar ReShade literalmente (parsear su lenguaje
ReShadeFX, ejecutar presets `.fx` de la comunidad) - ver la sección
"Por qué no 'ReShade real'" más abajo. Recomiendo un catálogo curado y nativo
de 4-6 efectos, con el mismo patrón de implementación que ya usamos para
FXAA/TAA (una función `DrawBackbufferQuadXxx` + un `ConfigValue` + un
dropdown), más una exploración *separada y de mayor riesgo* de un SSAO
aproximado si el resultado del tier 1 no es suficiente.

## Contexto: ReShade ya funciona con Cemu hoy, por inyección externa

Esto no es hipotético - RPCS3 (otro emulador de consola de Sony con
renderizado Vulkan/OpenGL) ya es usado con ReShade inyectado externamente
por su comunidad, con "algunos problemas técnicos" reportados
([ReShade Forum - misc. emulators with depth buffer access][1]). Cemu, al
exponer una ventana Win32 estándar con swapchain Vulkan/OpenGL, es
igual de "inyectable" por la misma vía (`dxgi.dll`/`opengl32.dll` proxy de
ReShade) sin que nosotros hagamos nada - es la opción de costo cero que el
usuario ya está evitando a propósito ("sin tener que instalar reshade"), así
que el resto de este documento asume que el objetivo es una alternativa
**nativa**, no simplemente confirmar que la externa funciona.

## Tier 1: efectos de color puro - viables ahora

### Qué ya existe que sirve de base
El pipeline de post-proceso de salida (construido esta sesión para
TAA/FXAA/SMAA/FSR1) ya es, estructuralmente, un sistema de shaders de
post-proceso encadenables:

- `RendererOutputShader` (`RendererOuputShader.h/.cpp`) - cada efecto es un
  shader de pantalla completa compilado una vez en `InitializeStatic()`
  (`RendererOuputShader.cpp:55`), con su propio uniform block
  (`FillUniformBlockBuffer`).
- Las funciones `DrawBackbufferQuadXxx` en `VulkanRenderer.cpp` (ej.
  `DrawBackbufferQuadFxaa`, `DrawBackbufferQuadTaa`) ya establecen el patrón
  exacto: target intermedio → pasada(s) de shader → blit final, reusando
  `backbufferBlit_createGraphicsPipeline`/`backbufferBlit_createDescriptorSet`
  (cacheados por hash de shader / por textura, ver
  `[[project_cemu_taa_jitter_binding_bug]]`).
- El dispatch en `LatteRenderTarget.cpp` (~línea 1059) ya selecciona qué
  función `DrawBackbufferQuadXxx` llamar según `CemuConfig::antialiasing_mode`
  - un efecto de post-proceso adicional es, literalmente, una rama más ahí,
    con su propio `ConfigValue` (mismo patrón que `taa_spatial_aa`).

Esto significa que **sharpening (CAS-style), corrección de color vía LUT
3D, viñeta, aberración cromática, grano de película o un filtro CRT/scanline
son, cada uno, "una función `DrawBackbufferQuadXxx` nueva + un shader GLSL
nuevo + un dropdown"** - el mismo esfuerzo por efecto que ya demostramos con
FXAA/SMAA/TAA esta sesión. No necesitan tocar el decompilador de Latte ni
saber nada de la semántica del juego, porque solo leen la imagen de color ya
compuesta - exactamente el mismo dato que FXAA ya lee hoy.

### Diseño sugerido: cadena encadenable, no un dropdown exclusivo
A diferencia de AA (donde FXAA/SMAA/TAA son mutuamente excluyentes por
diseño), estos efectos SÍ tienen sentido **encadenados** (sharpening +
viñeta + LUT de color, todos activos a la vez, como en un preset real de
ReShade). Esto es un cambio de forma respecto al dropdown de AA: en vez de
`antialiasing_mode` (un enum), necesitaríamos algo como
`post_effects_enabled` (bitmask o lista de `ConfigValue<bool>` por efecto) y
una función `DrawBackbufferQuadPostChain` que aplique cada efecto activo en
secuencia sobre el mismo par de targets intermedios ping-pong (reusar
`m_fxaaIntermediate*` como buffer A/B alternado), en vez de una función
dedicada por combinación como hicimos con FSR1+AA. Este es el único cambio
de arquitectura real que este tier necesita - todo lo demás es "más shaders
del mismo tipo que los que ya existen".

### Candidatos concretos, en orden de retorno/esfuerzo
1. **Sharpening tipo CAS** (AMD FidelityFX Contrast Adaptive Sharpening) -
   FSR1 ya trae RCAS (`s_fsr1_rcas_shader`), que es la mitad "sharpen" del
   mismo algoritmo de CAS - exponerlo como efecto standalone (no solo
   post-FSR1) es casi trabajo cero, ya está compilado y probado.
2. **LUT de color 3D** - estándar en post-proceso (Unity, Unreal, y el
   propio ReShade lo usan así): una textura 3D de 16³ o 32³ que mapea
   color-entrada → color-salida, cargable desde un `.png`/`.cube` como
   preset. Es el mecanismo detrás de la mayoría de los presets de "mejora de
   color" de ReShade.
3. **Viñeta / aberración cromática / grano de película** - shaders triviales
   de una sola pasada, sin dependencias.
4. **CRT/scanline** - popular para juegos de generaciones anteriores
   corridos a resoluciones altas; una sola pasada con muestreo por líneas.

## Tier 2: SSAO / sombras de contacto reales - mismo bloqueo que FSR2

### Qué existe hoy (investigado en el código)
- Cada framebuffer que arma el juego (`LatteMRT`/`LatteCachedFBO`,
  `LatteCachedFBO.h:22,63-80`) sí trackea su `depthBuffer.texture` **por
  draw call** - la profundidad NO está perdida a nivel de hardware emulado,
  igual que constató `FSR2_TAA_investigacion.md` punto 1.
- No existe ningún tracking *global* de "cuál fue la última/mayor superficie
  de profundidad usada este frame" (confirmado: `grep` por
  `depthBufferHash`/`lastDepthTarget`/`currentDepthTexture` en
  `Cafe/HW/Latte/Core/` no encuentra nada) - habría que construirlo desde
  cero.
- El punto de entrada del post-proceso (`LatteRenderTarget_copyToBackbuffer`,
  confirmado en `FSR2_TAA_investigacion.md` punto 5) solo recibe la imagen
  de color ya compuesta - ningún depth buffer llega hasta ahí hoy.

### El problema de fondo (por qué esto NO es solo plomería)
Un juego de Wii U típico dibuja a *varios* render targets con formato de
profundidad en un mismo frame: shadow maps (uno o más, con su propia
resolución/proyección), depth de la escena principal, depth de UI/HUD,
depth de efectos de partículas, etc. SSAO/contact-shadows necesitan
específicamente el depth de **la escena principal, en la proyección de
cámara del jugador** - no cualquiera de esos. Esto es exactamente el mismo
tipo de decisión semántica que bloqueó FSR2 (punto 6 de ese documento:
"Cemu no entiende qué buffer es qué, a propósito, porque emula comandos GX2,
no reconstruye la lógica del motor del juego").

**ReShade tiene el mismo problema, incluso con juegos modernos de PC**: su
propia documentación admite que usa heurísticas (mayor buffer por conteo de
draw calls, relación de aspecto, "copiar antes de un `clear`") y que "no es
perfecto", con overrides manuales por juego cuando falla
([ReShade Forum - depth buffer detection][1],
[Marty's Mods - depth buffer guide][2]). Es decir: **ni siquiera la
herramienta que el usuario quiere reemplazar resuelve esto de forma
genérica** - lo resuelve con la misma clase de heurística frágil que
`FSR2_TAA_investigacion.md` ya decidió NO perseguir para Cemu.

### Precedente real: Dolphin no corre ReShade, construyó su propio sistema
Dolphin (el emulador de GameCube/Wii con más trabajo de post-proceso hecho)
no ejecuta ReShade ni su lenguaje `.fx` - tiene su **propio** sistema nativo
de shaders de post-proceso con acceso a depth
([Dolphin PR #10362 - post processing shader system overhaul][3]), y la
comunidad porta manualmente shaders *inspirados* en ReShade a ese sistema
propio (proyecto DrakonaFX, para el fork Ishiiruka -
[github.com/Drakonas/DrakonaFX][4]) - esos shaders NO corren en el Dolphin
normal, están reescritos contra la API nativa. Es la validación más directa
que existe de que el camino correcto es "sistema propio, inspirado en
ReShade", no "ejecutar ReShade adentro".

### Si se quisiera intentar de todos modos (alto riesgo, no recomendado como próximo paso)
1. Agregar tracking global: "el depth buffer del framebuffer con más área
   dibujada este frame, justo antes del composite final" - heurística
   equivalente a la de ReShade, con los mismos falsos positivos esperables
   (shadow maps de alta resolución podrían ganarle al depth de escena en
   algunos juegos).
2. Llevar esa textura de profundidad como recurso adicional hasta
   `LatteRenderTarget_copyToBackbuffer` (plomería nueva, mismo tipo de
   trabajo que FSR2_TAA_investigacion.md ya estimó factible pero no
   trivial para *solo* depth).
3. SSAO en sí (ej. variante simple tipo SSAO clásico de Crysis, no HBAO+/GTAO)
   es un shader conocido y no muy caro, pero su *calidad final* depende
   enteramente de que el paso 1 haya acertado el buffer correcto - si falla,
   el resultado es oclusión visualmente incorrecta (manchas oscuras donde no
   corresponde), peor que no tener el efecto.
4. Validar contra un roster amplio de juegos (no solo BOTW/Splatoon), como
   ya recomendó `TEXTURE_SHADOW_FILTERS_investigacion.md` para PCF.

## Por qué no "ReShade real" (parsear `.fx`/ReShadeFX)

Aunque técnicamente posible (ReShadeFX es un lenguaje documentado, hay
implementaciones de referencia open-source), esto es un compilador +
runtime completo aparte - efectivamente escribir un segundo front-end de
shaders paralelo al que Cemu ya tiene para Latte/GX2, para ganar
compatibilidad con miles de presets de la comunidad que en su mayoría
**asumen acceso a depth/normales que Cemu tampoco tiene** (ver Tier 2). El
retorno real (poder cargar un preset `.fx` cualquiera de internet) es bajo
comparado con el costo, porque la mayoría de esos presets no funcionarían
igual de todos modos sin depth confiable. Un catálogo nativo y curado (Tier
1) da el 80% del beneficio percibido (los efectos que la gente más nota:
sharpening y color) por una fracción del costo.

## Veredicto y orden sugerido

1. **Tier 1 (color-only) primero** - mismo patrón de riesgo/esfuerzo que
   FXAA/SMAA/TAA/FSR1, que ya demostramos funciona bien esta sesión.
   Empezar por sharpening standalone (reusa RCAS ya compilado) y una LUT de
   color 3D - son los dos efectos de mayor impacto percibido por menor
   esfuerzo.
2. **Cambio de forma UI**: de dropdown-exclusivo (como AA) a lista de
   efectos encadenables con on/off independiente - único cambio
   arquitectónico real de este tier.
3. **Tier 2 (SSAO/sombras reales) como exploración aparte, de alto riesgo**
   - no lo recomiendo como siguiente paso. Si se persigue, tratarlo como un
   prototipo experimental en 2-3 juegos de prueba (mismo espíritu que la
   alternativa #2 que `FSR2_TAA_investigacion.md` sugirió para TAA
   "heurístico"), no como una feature general, y comunicar esa expectativa
   por adelantado.
3. **No perseguir compatibilidad real con presets `.fx` de ReShade** - costo
   de un segundo compilador de shaders para un beneficio que Tier 2 ya
   muestra que sería parcial en la práctica.

[1]: https://reshade.me/forum/general-discussion/5442-release-misc-emulators-with-depth-buffer-access
[2]: https://guides.martysmods.com/reshade/depth/
[3]: https://github.com/dolphin-emu/dolphin/pull/10362
[4]: https://github.com/Drakonas/DrakonaFX
