# Investigación: qué necesitaría FSR2 + TAA en Cemu

## Resumen ejecutivo

FSR2 no es "FSR1 pero mejor" — es un upscaler **temporal**, arquitectónicamente distinto.
Necesita datos por-frame que Cemu no puede obtener de forma genérica para juegos de
Wii U, porque esos datos requieren que el emulador entienda la *semántica* del
renderizado del juego (qué buffer es la matriz de proyección, qué buffer es
profundidad de la escena principal, etc.), y hoy Cemu trata todo eso como memoria
opaca. Los dos bloqueos son:

1. **Vectores de movimiento** - no existen, y no hay manera genérica de derivarlos.
2. **Jitter de la matriz de proyección** - requeriría inyectar sub-píxeles en el
   render del propio juego, y Cemu no sabe qué uniform buffer es esa matriz.

Todo lo demás (profundidad como textura, buffers persistentes entre frames, formatos
HDR) es plomería nueva pero factible. Los dos puntos de arriba no lo son de forma
genérica - ver la sección "Veredicto" al final.

## Qué necesita FSR2 (según el SDK público de AMD FidelityFX)

Por cada frame, FSR2 espera:

| Recurso | Descripción | Obligatorio |
|---|---|---|
| `color` | Color actual, resolución de render, con jitter aplicado | Sí |
| `depth` | Profundidad actual, misma proyección con jitter que `color` | Sí |
| `motionVectors` | Vector de movimiento por píxel, espacio de pantalla, frame actual → frame anterior | Sí |
| `jitterOffset` | Offset sub-píxel aplicado a la proyección este frame (secuencia Halton(2,3) recomendada) | Sí |
| `reactive` | Máscara de píxeles "reactivos" (partículas aditivas, reflejos) que no deben confiar en historial | Recomendado |
| `transparencyAndComposition` | Máscara de objetos transparentes/compuestos | Recomendado |
| `exposure` | Valor de exposición (o auto-exposición interna) | Opcional |
| `reset` | Flag para cortes de cámara (evita ghosting) | Sí |
| `frameTimeDelta`, `sharpness`, near/far de cámara | Parámetros escalares | Sí |

Internamente, FSR2 mantiene **recursos persistentes entre frames** (profundidad
reconstruida del frame anterior, buffers de "lock"/historial de luminancia, historial
de color) y se implementa como ~6-8 **compute shaders** (reconstruir profundidad
anterior, depth-clip, crear locks, reproyectar+acumular, RCAS final), no como los
fragment shaders de pantalla completa que usamos para FSR1/SMAA.

## Qué existe hoy en Cemu (investigado en el código)

### 1. Profundidad como textura - SÍ existe, pero no donde hace falta
Latte ya trata las superficies de profundidad como `LatteTexture`/`LatteTextureView`
normales (flag `isDepth`, `LatteTexture.h:72`), soporta `D24_S8_UNORM`,
`D24_S8_FLOAT`, `D32_FLOAT` (`LatteReg.h:289-293`), y Vulkan las mapea a formatos
sampleables (`VulkanRenderer.cpp:2507-2544`). El problema: eso solo existe **durante
los draw calls del propio juego** (para sus propios shadow maps, etc.). Para cuando
llega la etapa de composición final donde hoy conectamos FSR1/SMAA
(`LatteRenderTarget_copyToBackbuffer`, `LatteRenderTarget.cpp:865`), ya no hay acceso
al depth buffer de la escena - solo al color ya compuesto.

### 2. Vectores de movimiento / cualquier concepto temporal - NO existe
- El propio port de SMAA que hicimos tiene `#define SMAA_REPROJECTION 0`
  (`RendererOuputShader.cpp:1184`) - la ruta de reproyección con velocity buffer del
  SMAA de referencia está literalmente deshabilitada y ni se compila
  (`:1211-1258`).
- No hay ningún tracking semántico de matrices de vista/proyección. Los uniforms de
  GX2 llegan a Cemu como escritura de registros/memoria opaca
  (`GX2SetVertexUniformReg`/`GX2SetPixelUniformReg`, `GX2_Shader.cpp:446-463`) sin
  ninguna interpretación de qué representan esos floats.
- `GX2SetViewport`/`GX2SetScissor` (`GX2_State.cpp:214-269`) solo controlan la
  transformación del rasterizador, no ninguna matriz de proyección del juego.

**No hay ningún mecanismo existente que se pueda extender para derivar vectores de
movimiento.**

### 3. Buffers persistentes entre frames - NO existe
Todo lo que construimos para FSR1/SMAA (`m_fsr1EasuIntermediate*`,
`m_fxaaIntermediate*`, `m_smaaEdges*`/`m_smaaBlend*`) se recrea al resize pero vive
solo **dentro de un frame**. El único precedente de "leer datos de otro momento" es
la captura de screenshot (`VulkanRenderer.cpp:1015-1283`), que es un readback
puntual, no un historial recurrente. El punto natural donde enganchar esto sería
`Renderer::SwapBuffers` (`Renderer.h:69`, invocado desde
`LatteRenderTarget.cpp:686`).

### 4. Formatos de color - hoy todo es 8-bit
GX2 sí soporta superficies float (`R16_G16_B16_A16_FLOAT`, `LatteReg.h:264`) para lo
que el JUEGO renderiza, pero todo nuestro pipeline de post-proceso usa el formato del
swapchain de Vulkan (`chainInfo.m_surfaceFormat.format`, típicamente
`VK_FORMAT_B8G8R8A8_UNORM`, 8-bit). FSR2 necesita mínimo RGBA16F para color/historial
- habría que añadir soporte de render targets float que hoy no existe en la
infraestructura de intermedios.

### 5. Punto de entrada del pipeline de output-shader
Confirmado: `LatteRenderTarget_copyToBackbuffer(LatteTextureView* textureView, bool
isPadView)` (`LatteRenderTarget.cpp:865`) solo recibe la imagen de color YA
compuesta, su tamaño, y el área de salida. No tiene acceso al depth buffer del juego
ni a ningún uniform - eso vive antes, por-draw-call, dentro de
`LatteCommandProcessor.cpp`/`LatteRenderTarget.cpp:555-822`, y ya se perdió para
cuando llegamos a esta etapa. Llevar profundidad (o cualquier dato por-draw) hasta
aquí requiere plomería nueva.

### 6. Inyectar jitter en la proyección del juego - NO es posible de forma genérica
Mismo problema que el punto 2: como Cemu no entiende qué uniform buffer es la matriz
de proyección de cada juego, no hay gancho genérico para sumarle un jitter sub-píxel
antes de que el juego la use. Solo sería viable con heurísticas por-juego (detectar
"el buffer de 4x4 floats que se actualiza cada frame y tiene la forma característica
de una matriz de perspectiva"), lo cual es fragil y necesitaría validarse juego por
juego - no es un hook de motor genérico.

## Veredicto

**FSR2 genérico (que funcione automáticamente en cualquier juego de Wii U) no es
viable con la arquitectura actual de Cemu.** El motivo de fondo no es falta de
plomería (eso se puede construir) sino que los dos requisitos centrales de FSR2 -
vectores de movimiento reales y jitter de cámara - necesitan que el emulador
entienda la semántica del renderizado de cada juego, algo que Cemu deliberadamente
NO hace (emula el hardware a nivel de comandos GX2, no reconstruye la lógica del
motor del juego). Los juegos de Wii U además son anteriores a que FSR2/TAA con
vectores de movimiento fuera estándar, así que ningún juego expone esos datos por su
cuenta.

### Alternativas reales, en orden de lo más a lo menos realista

1. **Quedarnos con FSR1 + SMAA** (lo que ya está estable) y en su lugar exponer el
   sharpness de RCAS como control (la mejora pendiente que ya discutimos) - el
   camino de menor riesgo y mayor retorno inmediato.
2. **TAA "de cámara únicamente" experimental, sin FSR2 real**: inyectar jitter vía
   heurística en un solo juego de prueba (ej. Splatoon) para validar si la
   detección de matriz de proyección es viable ahí, aceptando ghosting en objetos
   animados (solo se reproyecta el movimiento de cámara, no el de cada objeto). Esto
   es un prototipo de investigación, no una feature general - probablemente
   rompería o no aplicaría en la mayoría de otros juegos sin repetir el trabajo de
   detección por título.
3. **Descartar el vector de movimiento temporal por completo** y en su lugar
   invertir ese esfuerzo en un upscaler espacial mejor (mejorar FSR1, o portar algo
   como una variante contrast-adaptive más agresiva) - mejora real, sin la
   fragilidad de heurísticas por-juego.

No recomiendo intentar FSR2 real como próximo paso: el trabajo de detección
heurística por-juego (punto 2) es semanas de esfuerzo con alta probabilidad de
resultados inconsistentes entre títulos, y no hay atajo arquitectónico que lo
evite dado cómo Cemu está diseñado hoy.
