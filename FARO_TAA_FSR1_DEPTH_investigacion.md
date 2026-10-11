# Faro: TAA, FSR1, scene-depth y giroscopio-mouse — investigación y cambios

Fecha: 2026-10-03. Rama: `faro-parches` (detached en `c0b2688`).

Documento de trabajo: qué se investigó, qué se cambió (ya compila), y qué queda
pendiente con el plan concreto para hacerlo.

---

## 1. Giroscopio con mouse — por qué no funcionaba

### Causa raíz (confirmada, no hipótesis)

`InputSettings2::on_controller_settings` (`src/gui/wxgui/input/InputSettings2.cpp`)
es un `switch` sobre `controller->api()` que **no tenía un `case InputAPI::Mouse`**.
El botón de ajustes ("engranaje") sí se habilita para el ratón, porque
`InputSettings2::has_settings()` devuelve `true` por defecto — pero al pulsarlo el
`switch` caía al vacío y no abría ningún diálogo.

Consecuencia en cadena:

1. El diálogo `DefaultControllerSettings` es el único sitio donde existe la casilla
   **"Use motion"**. Sin diálogo, no hay forma de activarla desde la GUI.
2. `ControllerBase::Settings::motion` vale `false` por defecto
   (`src/input/api/Controller.h`).
3. `ControllerBase::use_motion()` es `has_motion() && m_settings.motion` → `false`.
4. `EmulatedController::has_motion()` / `get_motion_data()` recorren los
   controladores y **saltan** cualquiera cuyo `use_motion()` sea falso.
5. `VPADController::update_motion()` está dentro de `if (has_motion())` → no se
   ejecuta nada.

Es decir: el ratón se detectaba, se podía añadir, se podía pulsar su engranaje… y la
ruta de movimiento nunca se leía.

### Evidencia

`bin/portable/controllerProfiles/controller0.xml` (el perfil activo, porque existe
`bin/portable/`) contiene:

```xml
<controller>
    <api>Mouse</api>
    <uuid>mouse</uuid>
    <display_name>Mouse</display_name>
    <motion>false</motion>
    ...
</controller>
```

`motion` **sí** se guarda y se carga correctamente (`InputManager::save` línea ~502,
`InputManager::load` línea ~170), así que ese `false` es el estado real y efectivo.

Esto implica algo que conviene decir claro: con este perfil el giroscopio-mouse
**tampoco estaba funcionando en tu PC**. Lo que funcione ahí es otra cosa (por
ejemplo el mapeo de ratón al touchscreen del GamePad, que Cemu ya traía de serie y es
independiente de este proveedor).

### Causas secundarias, reales y específicas de cada PC

Aunque lo anterior era el bloqueo principal, había tres cosas más que habrían hecho
que el comportamiento difiriera entre máquinas:

| Problema | Efecto en otro PC |
|---|---|
| `RegisterRawInputDevices` con `dwFlags = 0` | Windows solo entrega `WM_INPUT` mientras la ventana registrada está en *foreground*. Con la vista separada del GamePad abierta y enfocada (`PadViewFrame` es otra ventana top-level), la ventana principal deja de ser foreground y el mouse-look muere. |
| Solo se leía la rama relativa (`(usFlags & MOUSE_MOVE_ABSOLUTE) == 0`) | Los dispositivos que reportan posición **absoluta** no producían ni un delta: touchpads de precisión de Windows, tabletas/digitalizadores, ratones virtuales de VM y de escritorio remoto/streaming (RDP, Parsec, Moonlight) y algunos KVM. En esos PCs la función estaba completamente muerta. |
| Sensibilidad fija en `0.0045` | El rango útil depende del DPI del ratón. La misma constante que va bien a 800 DPI va ~5× demasiado rápida a 4000 DPI, lo que se percibe como "está roto". |

### Cambios aplicados

- `InputSettings2.cpp`: `case InputAPI::Mouse:` añadido al grupo que abre
  `DefaultControllerSettings` → la casilla "Use motion" ya es accesible.
- `MouseController`: `set_use_motion(true)` en el constructor. El controlador de ratón
  no tiene ejes ni mapeo por defecto, así que con `motion` apagado no hace nada;
  encenderlo por defecto es lo correcto. **Ojo:** un perfil que ya guardó
  `<motion>false</motion>` sigue ganando, porque `InputManager::load` aplica el valor
  almacenado después. Hay que marcar la casilla una vez (o borrar esa línea del XML).
- `MainWindow.cpp`: registro con `RIDEV_INPUTSINK`, y el filtro "no mover la cámara
  estando en otra app" se aplica donde se consumen los deltas, comprobando que la
  ventana en foreground pertenezca a este proceso. Resultado: funciona con la vista
  del GamePad enfocada y con cualquier distribución de ventanas.
- `MainWindow.cpp`: rama para `MOUSE_MOVE_ABSOLUTE`, reconstruyendo el delta por
  diferencia contra el paquete anterior y escalando a píxeles para que la
  sensibilidad signifique lo mismo en ambos tipos de dispositivo.
- `MainWindow.cpp`: se registra en el log si `RegisterRawInputDevices` falla.
- `MouseController` + `DefaultControllerSettings`: **sensibilidad** (slider) e
  **invertir horizontal / vertical** (casillas), persistidos en el perfil vía
  `MouseController::save/load` (`mouse_sensitivity`, `mouse_invert_x`,
  `mouse_invert_y`). Los inversores existen porque el mapeo de ejes
  (horizontal→yaw/gz, vertical→pitch/gx) se eligió por convención, sin test en un
  juego real; así el jugador lo corrige en vez de quedarse con un eje al revés.

### Lo que tu amigo tiene que hacer (y tú también)

1. Input settings → seleccionar API **Mouse** → añadir el dispositivo.
2. Pulsar el **engranaje** → marcar **"Use motion"** → ajustar sensibilidad.
3. **El controlador emulado tiene que ser "Wii U GamePad".** Solo
   `VPADController` (GamePad) y `WPADController` (Wiimote) leen movimiento;
   `ProController` y `ClassicController` no tienen ninguna ruta de motion, así que con
   un Pro Controller emulado el giroscopio no hace nada por diseño.
4. Si hay otro mando con motion activado en el mismo controlador emulado, gana el
   primero de la lista: `EmulatedController::get_motion_data()` devuelve el primero
   cuyo `use_motion()` sea verdadero.

---

## 2. Scene depth estilo ReShade `generic_depth` (SSAO + SSR)

### Qué hacía Faro antes

`LatteMRT::FindSceneDepthViewForSize` elegía "la vista de profundidad usada más
recientemente cuyo tamaño efectivo coincide **exactamente** con el tamaño de salida",
más 4 frames de gracia en `Renderer::CaptureDepthForPostFX`.

### Qué hace ReShade

Referencia leída: `examples/09-depth/generic_depth_addon.cpp` de
[crosire/reshade](https://github.com/crosire/reshade). Es el add-on integrado que
resuelve exactamente este problema. Su heurística:

1. **Estadísticas por frame y por buffer de profundidad**: vértices, draw calls,
   draw calls indirectos y último viewport (`struct draw_stats`).
2. **Filtro de candidatos** (`on_begin_render_effects`): descarta los que tienen
   0 draw calls o ≤3 vértices, los no usados en el frame, los que acaban de
   aparecer, los multisample sin soporte de resolve, y los que no pasan
   `check_aspect_ratio`.
3. **Ranking** por carga de trabajo (`draw_stats::operator>`): el modo por defecto
   `prefer_vertices` usa vértices, y cae a draw calls si hay muchos draws indirectos
   (donde el conteo de vértices no es fiable).
4. **`check_aspect_ratio`**: exige misma relación de aspecto (delta ≤ 0.1) y escala
   dentro de 0.5×–1.85×, con un modo extra para múltiplos casi exactos de la
   resolución.
5. **`preserve_depth_buffers`** (opcional): copia el depth *antes* de cada clear,
   quedándose con el clear de mayor carga del frame, para juegos que limpian la
   profundidad antes del HUD o de un segundo pase 3D.

### Las dos debilidades que esto corrige

- **La recencia no es evidencia.** Un shadow map, un minimapa o un depth de UI que
  comparta resolución con la escena y se enlace *después* de ella en el mismo frame
  simplemente ganaba. Solo un criterio basado en cuánta geometría entró realmente en
  cada buffer distingue "la escena principal" de "otra cosa del mismo tamaño".
- **Coincidencia exacta sin plan B.** Cuando la escena 3D se renderiza a una
  resolución distinta de la que se presenta (cualquier graphic pack de resolución, y
  bastantes juegos de forma nativa) no coincidía nada y los efectos con dependencia de
  profundidad se apagaban de golpe en vez de degradarse.

### Cambios aplicados

- `LatteRenderTarget.cpp`: `FaroDepthStats` / `FaroDepthCandidate` con `current` y
  `lastFrame` por buffer, `s_currentDepthView` como objetivo de atribución, y
  `FaroDepthAspectMatches` (port de `check_aspect_ratio` con los modos
  "similar" y "múltiplos" fundidos — los graphic packs de Cemu usan múltiplos
  exactos 2×/3×/4×, que caen fuera de la banda 0.5×–1.85× de ReShade).
- `LatteMRT::NotifyDepthDrawcall(vertexCount, instanceCount)`, llamado desde
  `LatteCommandProcessor::executeDraw` — el punto de despacho común, así que
  Vulkan, OpenGL y Metal alimentan las mismas estadísticas.
- `LatteMRT::AdvanceDepthStatsFrame()`, llamado una vez por frame presentado desde
  `LatteRenderTarget_itHLESwapScanBuffer`.
- `FindSceneDepthViewForSize` reescrito con el orden de ReShade: filtrar y luego
  rankear por carga. **Añadido propio de Cemu**: una coincidencia exacta de
  resolución siempre supera a una solo compatible en aspecto, para que el caso
  habitual (la escena de un juego de Wii U *sí* mide lo mismo que lo que se presenta)
  se comporte igual que antes y el test relajado solo entre en juego cuando no hay
  coincidencia exacta.
- `Renderer::CaptureDepthForPostFX`: el log ahora dice qué buffer ganó, contra qué
  resolución de salida, y con cuántos draw calls/vértices. Cuando SSAO o SSR se vean
  mal en otro PC o en un juego concreto, esa línea dice si se eligió el buffer
  equivocado y con qué evidencia.

El SSAO/GTAO y el SSR consumen ambos `GetCapturedDepthView()`, así que los dos se
arreglan con este único cambio.

### Pendiente aquí: `preserve_depth_buffers`

No implementado. Hace falta si aparece un juego que limpia la profundidad a mitad de
frame (overlay en primera persona, segundo pase 3D, post-proceso que limpia). Requiere
un hook en el clear de profundidad de Cemu más una imagen de copia en
`VulkanRenderer`. Síntoma que lo delataría: SSAO/SSR correctos en unos juegos y
"planos" en otro concreto, con el log reportando un buffer válido.

---

## 3. FSR1

### Auditoría del port contra la referencia de AMD

Descargado `ffx-fsr/ffx_fsr1.h` de
[GPUOpen-Effects/FidelityFX-FSR](https://github.com/GPUOpen-Effects/FidelityFX-FSR) y
comparado línea a línea con `s_fsr1_easu_shader_source` y
`s_fsr1_rcas_shader_source`.

**El port es fiel.** `FsrEasuTapF`, `FsrEasuSetF`, la normalización de la dirección,
el estiramiento del kernel, el dering contra min/max de los 4 vecinos, y toda la
cadena de RCAS (`hitMin`/`hitMax`/`lobe`/`APrxMedRcpF1`) coinciden con la ruta AF1
(32 bits). El cálculo de `pp` equivale a lo que `FsrEasuCon` empaqueta en
`con0`. No encontré errores de algoritmo.

Lo que sí encontré:

1. **Lecturas fuera de rango (`texelFetch`), el problema de portabilidad real.**
   El kernel de 12 taps de EASU alcanza de −1 a +2 téxeles y la cruz de RCAS alcanza
   1 téxel, así que en el borde de la imagen direccionaban téxeles inexistentes.
   `texelFetch` con coordenada fuera de rango tiene resultado **indefinido** en GLSL y
   en la práctica los drivers difieren (negro, clamp, o memoria adyacente). Es
   exactamente la clase de fallo "aquí se ve bien, en la GPU de otro hay un marco
   oscuro o con basura". No se puede arreglar con un address mode del sampler porque
   `texelFetch` ignora el estado del sampler; la referencia de AMD lo evita por
   construcción usando `textureGather` sobre un sampler `CLAMP_TO_EDGE`.
   **Arreglado**: `FaroEasuFetch` y `FaroRcasFetch` clampean la coordenada entera.
   EASU clampea contra `textureSrcResolution` (el tamaño *efectivo* de la vista), así
   que tampoco lee la zona de padding de una textura mayor.
2. **Nitidez fija en el shader.** Estaba como `#define FSR_RCAS_SHARPNESS 0.2`, sin
   control. AMD pasa la nitidez como constante a través de `FsrRcasCon` precisamente
   porque no hay una única respuesta buena: depende del factor de escalado (los
   escalados agresivos necesitan más nitidez para no verse blandos) y del monitor.
   **Arreglado**: `CemuConfig::fsr_rcas_sharpness` (centésimas de "stop" de AMD,
   rango 0.00–2.00) + slider en Ajustes → Gráficos, entregado por el uniform
   `faroFsrParams.x`. El slider va invertido en pantalla (derecha = más nítido)
   porque la escala de AMD cuenta stops de *reducción*.
3. **Falta la ruta de denoise de RCAS.** `FSR_RCAS_DENOISE` en `ffx_fsr1.h` calcula un
   paso alto normalizado del vecindario y escala el lóbulo de nitidez con él, para
   concentrarse en bordes reales en vez de amplificar grano y dithering. AMD lo deja
   apagado y sugiere aplicar el grano *después* de RCAS — pero un emulador no puede
   reordenar el post-proceso del juego, y los títulos de Wii U sacan salida con
   dithering a menudo, así que aquí sí importa. **Arreglado**: `fsr_rcas_denoise` +
   casilla "FSR grain-aware sharpening".

### Nota de espacio de color (verificada, no hay nada que arreglar)

La [guía de integración de FSR1](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-spatial/)
exige entrada normalizada a [0,1] y **en espacio perceptual (sRGB, no lineal)**.
`SwapchainInfoVk::ChooseSurfaceFormat` prefiere `VK_FORMAT_B8G8R8A8_UNORM` con
`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`, es decir datos ya codificados en gamma dentro de
un contenedor UNORM. Los targets intermedios heredan ese formato. Correcto tal cual.

**Pero** hay un riesgo de portabilidad latente: ese `ChooseSurfaceFormat` termina en
`return formats[0]` si no encuentra su formato preferido. En un PC donde el driver no
exponga `B8G8R8A8_UNORM/SRGB_NONLINEAR` (p. ej. con HDR activado, donde suele
aparecer `A2B10G10R10_UNORM_PACK32`, o un `_SRGB`) todos los targets intermedios de
FSR1/FXAA/SMAA/TAA se crean con ese formato. Con un formato `_SRGB` el hardware
convierte en cada escritura y lectura, así que EASU/RCAS pasarían a operar en lineal
— peor calidad, en silencio y solo en ese PC. Ver la sección 4, que lo resuelve de
paso.

---

## 4. TAA — estado real y los dos techos

### Qué es hoy el modo "TAA" de Faro

No es un TAA. `LatteMRT::GetCurrentTaaJitter` devuelve cero a propósito: el jitter
está desactivado porque el pase de resolve no tiene reproyección, y jitterear la
cámara sin deshacerlo después produce shimmer. Lo que hace hoy `kAATaa` es un
**blend temporal adaptativo al movimiento** (clip AABB al estilo Karis en YCoCg,
historial con muestreo bicúbico, gamma de varianza que se abre con el movimiento)
alimentado por un pre-pase espacial (FXAA o SMAA).

El resolve en sí está bien hecho: usa clipping en YCoCg (no por canal en RGB), el
ray-clip de la caja de Karis 2014 y muestreo bicúbico del historial. No es ahí donde
está el problema.

### Techo 1: el historial es de 8 bits

`DrawBackbufferQuadFsr1Taa` y `DrawBackbufferQuadTaa` crean
`m_taaHistory*`, `m_taaNativeResolve*` y los targets espaciales con
`chainInfo.m_surfaceFormat.format` — normalmente `B8G8R8A8_UNORM`, 8 bits por canal.

El resolve mezcla con `blendCur` tan bajo como 0.10. En 8 bits el escalón de
cuantización es 1/255 ≈ 0.4%, así que un aporte del 10% solo mueve el resultado si
`|actual − historial| > ~4%`. Por debajo de eso **el resultado redondea al mismo
valor y la acumulación se detiene**: justo las diferencias sub-píxel que un TAA
debería integrar son las que desaparecen. Además introduce banding.

**Arreglo**: targets intermedios en `VK_FORMAT_R16G16B16A16_SFLOAT`. Es formato
obligatorio en Vulkan para `COLOR_ATTACHMENT` + `SAMPLED` + filtrado lineal con
tiling óptimo, así que además elimina el riesgo de portabilidad del formato de
swapchain descrito en la sección 3.

**Por qué no está hecho ya**: los pipelines de estos pases se crean en
`backbufferBlit_createGraphicsPipeline`, que los construye siempre contra
`chainInfo.m_swapchainRenderPass` y los cachea con un hash de
`(vertex shader, fragment shader, padView)`. Los render passes intermedios se crearon
deliberadamente *compatibles* con el del swapchain (mismo formato, mismo sample count,
mismo layout de subpass) para poder reutilizar esos pipelines. Cambiar el formato de un
target **rompe esa compatibilidad**: la compatibilidad de render passes en Vulkan exige
formatos de attachment iguales, y usar un pipeline creado para un render pass dentro de
otro incompatible es uso inválido.

El refactor necesario:

1. Añadir un parámetro `VkRenderPass` a `backbufferBlit_createGraphicsPipeline` e
   **incluirlo en el hash** del caché.
2. Pasar en cada uno de los **36** puntos de llamada el render pass de la instancia
   dentro de la que se va a usar el pipeline.
3. Cambiar a `R16G16B16A16_SFLOAT` los `Ensure*Target` de TAA (historial, resolve
   nativo, targets espaciales) y, opcionalmente, el intermedio de EASU — en ese caso
   RCAS deja de leer un intermedio de 8 bits, que es la otra fuente de banding, ya
   que RCAS amplifica la cuantización.

Es mecánico, pero son 36 sitios y un error de emparejamiento no da error de
compilación: da comportamiento inválido (pantalla negra, basura o cuelgue de GPU)
posiblemente solo en algunas GPUs. No lo dejo a ciegas sin un pase de verificación
visual, que solo se puede hacer ejecutando juegos.

### Techo 2: no hay motion vectors

Sin reproyección, el blend temporal solo puede promediar píxeles en la **misma**
coordenada entre frames. Eso limita a: o poco historial (apenas antialiasing), o mucho
historial (ghosting). Es por eso que el jitter está apagado — con reproyección, el
jitter pasa de ser un defecto a ser la fuente del supersampling.

Un juego nativo saca los motion vectors de su propio render. Un emulador no los tiene,
así que hay que estimarlos. Referencias estudiadas:

- **[JakobPCoder/ReshadeMotionEstimation](https://github.com/JakobPCoder/ReshadeMotionEstimation)**
  (`MotionEstimation.fx` + `.fxh`): flujo óptico denso por **block matching sobre una
  pirámide** de 8 niveles. Por nivel: bloque local del frame actual, búsqueda
  iterativa en círculos alrededor de un centro heredado del nivel inferior, coste
  SAD, y propagación hacia arriba con `UpscaleMotion`, que pondera por profundidad,
  pérdida, nivel de detalle y longitud del vector para suprimir vectores poco
  fiables.
- **[JakobPCoder/ReshadeTFAA](https://github.com/JakobPCoder/ReshadeTFAA)**
  (`TFAA.fx`): el consumidor. Lo relevante que hace y nosotros no:
  **motion vector dilatado** (toma el MV del vecino más cercano en profundidad de un
  3×3, reduce artefactos en bordes de objetos móviles), **máscara de desoclusión** por
  delta de profundidad, peso de blend modulado por velocidad y contraste local,
  mezcla en espacio elevado a potencia para preservar altas luces, clamp en YCbCr
  *después* de mezclar, y un **post-sharpen adaptativo** tipo CAS cuya fuerza sube
  con velocidad × contraste, para compensar el desenfoque temporal.
- **[iMMERSE Launchpad](https://www.martysmods.com/launchpad/)** de Marty McFly:
  block matching jerárquico de 8 niveles con descenso de gradiente estocástico. Es
  el estado del arte en ReShade pero no tiene fuente pública utilizable.

**Restricción de licencia, importante porque distribuyes binarios:** tanto TFAA como
ReshadeMotionEstimation son **CC BY-NC 4.0** (no comercial), incompatible con la
MPL-2.0 de Cemu. Su código **no puede copiarse** a este árbol. La *técnica* (block
matching piramidal es de los años 80, de la codificación de vídeo; variance clipping,
dilatación de MV y desoclusión por profundidad están publicados en cursos de
SIGGRAPH — Karis 2014, Jimenez 2016) sí es utilizable: hay que escribir una
implementación propia.

A favor nuestro: **ya tenemos profundidad de escena** en el momento de presentar
(`Renderer::CaptureDepthForPostFX`, ahora con la heurística de la sección 2), y la
sección 2 la hace fiable. Eso habilita la desoclusión por profundidad y el coste
guiado por profundidad sin trabajo adicional.

### Plan por fases para el TAA

| Fase | Trabajo | Riesgo |
|---|---|---|
| A | Targets en `R16G16B16A16_SFLOAT` + pipelines conscientes del render pass (36 sitios) | Medio. Mecánico pero necesita verificación visual. Es el mayor salto de calidad por unidad de trabajo. |
| B | Mejoras del resolve que no necesitan MVs: pesos de mezcla tonemapeados al estilo Karis (`c/(1+luma)`) contra fireflies, y post-sharpen adaptativo | Bajo. Se puede hacer antes o después de A. |
| C | Estimador de movimiento propio: pirámide de gris, block matching con SAD, refinamiento por nivel, salida `RG16F` a resolución nativa | Alto. Varios targets e imágenes nuevas, coste de GPU real, hay que medirlo. |
| D | Resolve reproyectado: MV dilatado por profundidad más cercana, desoclusión por delta de profundidad + confianza del matching, blend según velocidad | Medio, depende de C. |
| E | Reactivar el jitter de Halton en `GetCurrentTaaJitter` (el mecanismo de push constant ya está puesto y funciona) | Bajo, depende de D. Aquí es donde el TAA pasa a ser un supersampler de verdad. |

A y B son las que dan más por menos. C/D/E son el TAA "de verdad" y son un proyecto
propio.

---

## 5. Cómo compilar (quedó anotado porque cuesta encontrarlo)

El generador es **Ninja**, pero `cl.exe` necesita el entorno de MSVC (`INCLUDE`,
`LIB`): sin él falla con `fatal error C1083: stdlib.h: No such file or director  y`, que
parece un error del código y no lo es.

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cd /d C:\Users\devam\OneDrive\Documentos\Aurora-Launcher\emulators\cemu
ninja -C build CemuBin
```

Salida: `bin/Cemu_release.exe`. Todos los cambios de este documento compilan sin
errores ni warnings nuevos.

---

## 6. Resumen de archivos tocados

Giroscopio-mouse:
- `src/gui/wxgui/input/InputSettings2.cpp`
- `src/input/api/Mouse/MouseController.h` / `.cpp`
- `src/gui/wxgui/input/settings/DefaultControllerSettings.h` / `.cpp`
- `src/gui/wxgui/MainWindow.h` / `.cpp`

Scene depth (SSAO + SSR):
- `src/Cafe/HW/Latte/Core/LatteCachedFBO.h`
- `src/Cafe/HW/Latte/Core/LatteRenderTarget.cpp`
- `src/Cafe/HW/Latte/Core/LatteCommandProcessor.cpp`
- `src/Cafe/HW/Latte/Renderer/Renderer.cpp`

FSR1:
- `src/Cafe/HW/Latte/Renderer/RendererOuputShader.h` / `.cpp`
- `src/config/CemuConfig.h` / `.cpp`
- `src/gui/wxgui/GeneralSettings2.h` / `.cpp`

---

## 7. Fases A y B: implementadas y corregidas

### El bug de la pantalla negra

La primera pasada de la fase A introdujo el parámetro `VkRenderPass` en
`backbufferBlit_createGraphicsPipeline`, lo metió en el hash de la caché y pasó los
targets de TAA a `R16G16B16A16_SFLOAT`. Correcto de planteamiento, pero solo 15 de los
35 puntos de llamada recibieron un render pass, y **4 de ellos recibieron el
equivocado**:

| Línea | Pipeline creado contra | Render pass en que se usaba |
|---|---|---|
| 7864 | `m_taaNativeResolveRenderPass` (16F) | `chainInfo.m_swapchainRenderPass` (8 bits) |
| 7292 | `m_taaFxaaRenderPass` (16F) | `chainInfo.m_swapchainRenderPass` |
| 7253 | `m_taaNativeResolveRenderPass` | `m_fxaaIntermediateRenderPass` |
| 7380 | `m_taaNativeResolveRenderPass` | `m_fxaaIntermediateRenderPass` |

La línea 7864 es el **pase final de `DrawBackbufferQuadTaa`**, la ruta activa con
`antialiasing_mode = kAATaa` y FSR1 apagado. La compatibilidad de render passes en
Vulkan exige formatos de attachment iguales, así que un pipeline construido para un
render pass de 16 bits usado dentro del render pass del swapchain de 8 bits es uso
inválido: nada válido llega a pantalla. De ahí la pantalla negra.

Mientras todos los targets compartían el formato del swapchain estos 4 errores eran
invisibles, porque los render passes eran mutuamente compatibles. Cambiar el formato
los sacó a la luz de golpe.

Segundo foco: `RunFaroSsrPass` se invoca con `ssrOutRP = m_taaNativeResolveRenderPass`
o `m_taaFxaaRenderPass` (16F), pero su pipeline principal no se había actualizado y
caía al default del swapchain. El de GTAO sí estaba correcto.

### Lo corregido

- **Los 35 puntos de llamada** pasan ahora explícitamente el render pass de la
  instancia en que se usa el pipeline, verificado con un script que compara cada
  llamada contra la asignación `.renderPass =` que le sigue: 0 discrepancias, 0
  llamadas de 3 argumentos. Los 20 que antes funcionaban por el default del swapchain
  lo hacían solo por casualidad —sus targets seguían con formato de swapchain—, y eso
  es precisamente lo que permitió que el bug pasara desapercibido.
- **`InvalidateBackbufferBlitPipelineCache()`**, llamada antes de cada
  `vkDestroyRenderPass` de los 10 targets de post-proceso y en `RecreateSwapchain`.
  Sin esto quedaba un fallo latente: la clave de la caché incluye el *handle* del
  render pass, y Vulkan puede devolver el mismo valor de handle para un render pass
  nuevo. Si ese handle reciclado pertenece a un render pass con otro formato —justo lo
  que pasa al recrear un target por un resize— la caché devolvería un pipeline del
  formato antiguo. Es seguro destruir los pipelines ahí porque todos esos puntos ya
  hacen `WaitDeviceIdle()` antes.
- Formatos verificados por grupo: targets de TAA en 16F, el resto en formato de
  swapchain, sin ningún render pass compartido entre formatos distintos.

### Fase B: reescrita

La primera versión tenía dos errores de diseño:

```glsl
float weightLuma = 1.0 / (1.0 + lumaCur);
float blendCur = clamp(blendBase * (0.5 + 0.5 * weightLuma), 0.10, 0.85);
vec3 sharp = cM + 0.25 * (cM - hist);
```

1. **No era el pesado de Karis.** Karis pondera las *muestras* por `1/(1+luma)` antes
   de promediar y luego invierte la transformada. Escalar el factor de mezcla hace lo
   contrario de lo buscado: en zonas brillantes `weightLuma` baja, así que `blendCur`
   baja y el brillo retiene **más** historial → más ghosting en altas luces, que es
   exactamente el artefacto que esa ponderación existe para eliminar.
2. **`cM - hist` es una diferencia temporal, no un sharpen espacial.** Mezclar hacia
   eso con fuerza proporcional a `ghost` amplifica los píxeles que ya discrepan entre
   frames, o sea fabrica shimmer en vez de detalle.

Versión actual:

- Mezcla en el espacio tonemapeado `c / (1 + luma(c))` con inversa
  `c / max(1 - luma(c), 1e-4)` — la forma publicada, que reweighta las muestras para
  que una sola muy brillante no domine el promedio (fireflies en especulares y
  vegetación).
- Suelo de `blendCur` bajado de 0.10 a **0.05**, que es lo que compra la precisión de
  16 bits: a 8 bits una aportación de 0.05 es menor que un escalón de cuantización
  para cualquier diferencia por debajo de ~8%, así que la acumulación se detenía. 0.05
  son ~20 frames de acumulación en imagen quieta, y `ghost` vuelve a abrir la mezcla en
  movimiento.
- Compensación del desenfoque temporal con **unsharp mask contra la cruz espacial**
  (`cN/cS/cE/cW`), con fuerza `0.5 * (1 - blendCur)`: más corrección donde más se
  difuminó (imagen quieta), casi nada en movimiento rápido.
- **Guarda anti-ringing** que limita el overshoot del unsharp a la caja del vecindario
  **ensanchada para incluir `resolved`**. Ese ensanchado es lo importante: acotar solo
  al min/max del frame actual descartaría los valores acumulados temporalmente que
  caen fuera del vecindario de este frame, que son justamente la salida del filtro
  temporal.

### Pendiente

- `EnsureFsr1EasuIntermediateTarget` sigue a 8 bits. En la ruta FSR1+TAA el resolve
  sale a 16F y EASU lo reescribe a 8 bits acto seguido, y RCAS amplifica esa
  cuantización. Pasarlo a 16F es la continuación natural, pero amplía el radio del
  cambio y conviene validar antes lo ya hecho.
- Fases C/D/E (motion vectors propios, resolve reproyectado, reactivar el jitter de
  Halton) sin empezar — ver la sección 4.

---

## 8. Segunda ronda: crash en pantalla completa, depth mal elegido, desenfoque

### Crash al pasar a pantalla completa (`0xc0000409`)

`0xc0000409` es `STATUS_STACK_BUFFER_OVERRUN`, es decir un abort deliberado, no un
access violation — mismo offset en dos crashes consecutivos.

Lo causó **la propia invalidación de caché de la sección 7**. `DrawBackbufferQuadFsr1Taa`
crea sus 6 pipelines (pre-pase espacial, resolve, EASU, RCAS) y solo *después*, en la
línea 7083, llama a `EnsureFxaaIntermediateTarget` para la ruta de GTAO. Al entrar en
pantalla completa cambia `imageWidth/imageHeight`, ese `Ensure` destruye su target,
cae en `InvalidateBackbufferBlitPipelineCache()` → **destruía los pipelines que esa
misma función seguía teniendo en variables locales**, y acto seguido los enlazaba.

`WaitDeviceIdle()` solo demuestra que la GPU terminó; no dice nada sobre handles que
la CPU todavía sostiene más arriba en la pila de llamadas.

Corregido: la invalidación mueve los handles a un *graveyard* y **no destruye nada**.
Lo que evita la reutilización obsoleta es vaciar el mapa, no destruir; los handles se
liberan en el destructor del renderer junto con los vivos. Coste: un puñado de objetos
pipeline por resize.

### El depth elegido no era la escena

El log de la sesión mostró:

```
Faro depth capture: using depth 1280x720 for post FX (output 1920x1080, 60 drawcalls / 382 vertices last frame)
```

382 vértices no es una escena 3D. Bug en el port de `generic_depth`:
`NotifyDepthAttachmentUsed` solo se invoca dentro del `if (depthBuffer.view)` de
`ApplyCurrentState`, así que al enlazar un framebuffer **sin** depth `s_currentDepthView`
quedaba obsoleto y todos los draws sin profundidad (HUD, UI, quads de post-proceso
a pantalla completa) se acreditaban al último depth enlazado. Eso es justo el ruido
que descoloca el ranking por carga de trabajo.

Corregido con la rama `else` que llama a `NotifyDepthAttachmentUsed(nullptr)`.
La línea de log de arriba es la forma de comprobarlo: la escena real debería mostrar
miles de draw calls y cientos de miles de vértices.

### Desenfoque del TAA

Es estructural mientras no haya motion vectors (techo 2 de la sección 4): sin
reproyección, todo historial retenido es historial que la cámara ha movido por debajo,
así que mezclarlo difumina. Pero además la sección 7 lo **empeoró** al bajar el suelo
de `blendCur` de 0.10 a 0.05, o sea hasta un 95% de historial.

En vez de adivinar una constante, ahora son dos ajustes (Ajustes → Gráficos), que
viajan en los slots `z`/`w` libres de `faroFsrParams` para no tocar el layout std140:

| Ajuste | Rango | Defecto | Efecto |
|---|---|---|---|
| `TaaHistory` | 0–100 | **90** | Suelo de `blendCur` = `1 - 0.95*(h/100)`. 90 reproduce exactamente el 0.10 anterior, así que no hay regresión respecto a la línea base. 100 = historial más largo (más AA, más blando), 0 = sin historial. |
| `TaaSharpness` | 0–100 | 50 | Escala del unsharp mask espacial. |

Para bajar el desenfoque: reducir `TaaHistory` a ~60–70, y cambiar el pre-pase espacial
del TAA de **FXAA a SMAA** — FXAA es difuso de por sí y corre *antes* de la mezcla
temporal, así que su suavizado se acumula con el del historial.

---

## 9. ¿Es el `generic_depth` de ReShade? No del todo — qué falta

Se portó la **heurística de selección**, no el add-on completo. Comparación pieza a
pieza con `examples/09-depth/generic_depth_addon.cpp`:

| Pieza de ReShade | Estado en Faro |
|---|---|
| Estadísticas por frame y buffer (vértices / draw calls) | portado |
| Filtros de candidato (sin uso, recién aparecido, aspecto) | portado |
| Ranking `prefer_vertices` | portado |
| `check_aspect_ratio` | portado, con el modo de múltiplos fundido |
| **Selección pegajosa** (no suelta la anterior si un frame no encuentra ganador) | **añadido en la sección 10** |
| **`preserve_depth_buffers`: copia del depth ANTES de cada clear** | **falta** |
| **Textura de respaldo: los shaders leen una COPIA, no el buffer vivo** | **falta** |
| Segmentación de estadísticas por clear (`stats.current`, `clears[]`) | falta |
| Detección de depth invertido (`reversed_clear_value`) | falta |
| Resolve de multisample | no aplica en Cemu |
| Filtro por formato, resolución personalizada, selector manual en UI | falta (opcional) |

### Por qué importa cada una de las que faltan

**Textura de respaldo.** ReShade entrega a los shaders una *copia* del depth, tomada en
el momento correcto del frame — y en Vulkan y D3D12 lo hace **siempre**, no solo cuando
`preserve_depth_buffers` está activo, precisamente para no depender de lo que el juego
haga con el buffer después. Faro pasa el `LatteTextureView*` **vivo**: si el juego lo
limpia o lo reutiliza para otro pase después de la escena 3D, GTAO y SSR leen ese
contenido posterior. Eso produce un frame suelto con oclusión incorrecta, distinto del
flicker de disponibilidad de la sección 10.

**Copia antes del clear.** Es la opción que existe en ReShade justo para los juegos que
limpian la profundidad a mitad de frame (overlay en primera persona, segundo pase 3D,
post-proceso que limpia). Sin ella no hay forma de recuperar el contenido de la escena
en esos títulos.

**Segmentación por clear.** Nuestras estadísticas son totales por frame, así que un
buffer que el juego usa primero para un shadow map y luego para la escena suma ambos
y puede ganar por el motivo equivocado. ReShade reinicia `stats.current` en cada clear
y guarda la lista de clears para elegir el de mayor carga.

### Plan actualizado

| Fase | Trabajo | Estado |
|---|---|---|
| A | Targets 16F + pipelines conscientes del render pass | hecho (secciones 7 y 8) |
| B | Resolve: pesado de Karis, unsharp espacial, sliders | hecho (secciones 7 y 8) |
| **F** | **Snapshot de profundidad: imagen propia, copia tomada al final del pase 3D, y que GTAO/SSR lean la copia** | **siguiente** |
| G | Segmentación de estadísticas por clear + copia antes del clear | pendiente |
| H | `EnsureFsr1EasuIntermediateTarget` a 16F | pendiente |
| C | Estimador de movimiento propio (pirámide + block matching) | pendiente |
| D | Resolve reproyectado (MV dilatado, desoclusión) | pendiente |
| E | Reactivar el jitter de Halton | pendiente |

---

## 10. Flashes de SSAO/SSR: diagnóstico y arreglo

El log de una sesión real lo mostró directamente:

```
21:34:35  no scene depth buffer matched output 1920x1080   -> efectos OFF
21:34:35  using depth 1280x720 (4082 drawcalls / 3858235 vertices)
21:46:32  no scene depth buffer matched output 1920x1080   -> efectos OFF
21:46:38  using depth 1280x720 (2379 drawcalls / 1701586 vertices)
21:46:47  using depth 1280x720 (10 drawcalls / 30 vertices)  -> buffer equivocado
```

Dato de paso: la escena 3D de BOTW es **1280x720** y la salida 1920x1080, así que la
coincidencia solo ocurre gracias a la relajación de aspect ratio. Con el criterio de
tamaño exacto original no había profundidad en absoluto.

Dos modos de fallo, ambos corregidos:

**1. Caída a "sin profundidad".** La selección expiraba tras 4 frames sin coincidencia
y entonces `HasCapturedDepth()` pasaba a falso, apagando GTAO y SSR de golpe (van
hard-gated, sin suavizado propio). ReShade nunca hace eso: si un frame no encuentra
ganador, su bloque de actualización se salta y conserva el `selected_shader_resource`
anterior. Ahora Faro hace lo mismo — mantiene la última vista buena mientras siga
viva, y `NotifyTextureDeletion` la poda en el momento real en que la textura
desaparece, así que no puede quedar colgada.

**2. Ganaba un buffer casi vacío (10 draws / 30 vértices).** Los candidatos se
filtraban por las estadísticas del frame *anterior*, y los juegos alternan entre dos
buffers de profundidad en frames consecutivos: mirando solo el frame completado, el
buffer A reporta cero trabajo en todos los frames en que se dibujó en B y viceversa,
de modo que en un frame dado **ambos** parecen sin usar. Cuando el bueno quedaba
descartado, ganaba cualquier buffer basura del mismo tamaño. Ahora se juzga con
`max(current, lastFrame)` — `current` es válido y de hecho más fresco, porque esto
corre en el copiado al backbuffer, con la geometría del frame ya enviada — y el margen
de "usado recientemente" pasó de 1 a 2 frames.

---

## 11. Fase F implementada: snapshot de profundidad

ReShade entrega a los shaders una **copia** del depth (su "backup texture"), y en
Vulkan y D3D12 lo hace de forma incondicional. Faro pasaba el `LatteTextureView*`
**vivo**: una vez terminado el pase 3D el buffer vuelve a ser del juego, que puede
limpiarlo, renderizar un segundo pase o reutilizarlo antes de presentar — y lo que
contenga en ese momento es lo que leían GTAO y SSR.

### Cómo quedó

- **Imagen propia** (`m_faroDepthSnapshot*`) con el **mismo formato y tamaño** que la
  textura de origen, así la copia es un `vkCmdCopyImage` directo sin conversión. La
  vista usa **solo el aspecto DEPTH**, que es lo que la hace muestreable incluso
  cuando el origen es un formato combinado depth/stencil — dos de los cuatro que usa
  Cemu lo son (`D24_UNORM_S8_UINT`, `D32_SFLOAT_S8_UINT`).
- **Momento de la copia**: cuando el juego **desenlaza** el depth, que es el final de
  una secuencia de renderizado de profundidad. El equivalente de
  `clear_op::unbind_depth_stencil_view` de ReShade.
- **Qué desenlace copiar**: la regla `best_copy_stats` de ReShade — se copia cuando esa
  secuencia dibujó al menos tanta geometría como la mejor ya copiada en el frame. Un
  frame desenlaza varios buffers y esto converge en el más cargado (la escena) sin
  tener que saber de antemano cuál desenlace es el último. El contador se resetea por
  frame.
- **Degradación, no fallo**: si el formato no soporta ser destino de transferencia y
  muestreado (se comprueba con `vkGetPhysicalDeviceFormatProperties`, no se asume), o
  si la imagen no se puede crear, o si ningún desenlace del frame mereció copia,
  GTAO y SSR vuelven a leer el buffer vivo exactamente como antes. Nada depende de que
  esto funcione.
- Las barreras del buffer vivo se omiten cuando se lee la copia: ya está en
  `SHADER_READ_ONLY` y el buffer del juego deja de ser asunto de ese pase.
- Se loguea una vez por sesión cuando el snapshot queda activo, con formato y tamaño.

### Honestidad sobre la motivación

Esta fase se dedujo del **diseño** de ReShade, no de un síntoma observado en los logs.
La evidencia recogida apunta al ping-pong entre buffers del mismo tamaño (sección 10),
que se corrige con la histéresis. El snapshot cierra una vía distinta y real — frames
con contenido de profundidad incorrecto — pero que no está demostrada en este juego
concreto. Si tras la histéresis el flicker desaparece, el snapshot sigue siendo
correcto por diseño aunque su efecto no sea visible.

---

## 12. Fase F: device lost y corrección

Primera versión de la fase F: crash al entrar en la pantalla de carga.

```
22:10:31.119  Faro depth snapshot: active, 64x64 format 126
22:10:31.678  Unrecoverable error in Vulkan renderer
22:10:31.678  Msg: failed to submit command buffer. Error -4
```

`Error -4` es `VK_ERROR_DEVICE_LOST`. Dos fallos de diseño, visibles ambos en esas
dos líneas:

**1. `WaitDeviceIdle()` en mitad del frame.** `EnsureFaroDepthSnapshotTarget` recrea la
imagen cuando cambia el formato o el tamaño, y para ello tiene que esperar a que el
dispositivo quede libre. La copia se graba desde el camino de enlace de framebuffer,
o sea en mitad del frame, y el juego desenlaza buffers de profundidad de muchos
tamaños distintos — así que esa recreación se disparaba repetidamente dentro del
frame. Eso perdió el dispositivo. `WaitDeviceIdle()` solo demuestra que la GPU
terminó; no legitima reconstruir recursos en cualquier punto del frame.

**2. No se comprobaba QUÉ buffer se copiaba.** El `64x64` del log lo delata: la regla
de mejor-copia se aplicaba al primer desenlace con carga del frame, que fue un shadow
map o cara de cubemap, y GTAO habría muestreado eso como si fuera la escena de 720p.
Ese mismo fallo es el que provocaba el churn de recreación del punto 1.

### Corregido

- Solo se copia el buffer que **la heurística de selección ya eligió**
  (`GetCapturedDepthView()`). Elimina el caso 64x64 y, con él, casi todo el churn:
  el tamaño del buffer seleccionado es estable.
- La creación se **difiere**: un desajuste de formato/tamaño solo registra lo que hace
  falta y renuncia al snapshot de ese frame; `InvalidateSceneDepthSnapshot` hace la
  (re)creación en el swap, el mismo punto del frame donde todos los demás
  `Ensure*Target` ya hacen su propio `WaitDeviceIdle`. Cuesta un frame de fallback
  tras un cambio de resolución.
- **Apagado por defecto**, con la clave `FaroDepthSnapshot` en `settings.xml`
  (sección `<Graphic>`). ReShade también trae su equivalente
  (`preserve_depth_buffers`) apagado por defecto. Siendo el punto más intrusivo que
  toca este renderer, encenderlo o apagarlo debe ser un cambio de ajuste y no una
  recompilación.

---

## 13. El umbral heredado de ReShade era demasiado permisivo

Un log posterior mostró que la histéresis sola no bastaba:

```
22:16:53  using depth 1280x720 (10 drawcalls / 30 vertices)
22:16:54  switched to 1280x720 (4 drawcalls / 12 vertices)
22:16:56  switched to 1280x720 (10 drawcalls / 30 vertices)
22:16:59  switched to 1280x720 (4 drawcalls / 12 vertices)
```

Alternaba entre dos buffers con **30 y 12 vértices**. Ninguno era la escena: la real,
cuando aparece, reporta 4082 draw calls y 3.86M vértices. Ambos superaban el
`vertices <= 3` de ReShade, y con cargas tan pequeñas y ruidosas el factor 2x de la
histéresis se cruza con cualquier cosa.

Añadido un mínimo absoluto para ser considerado escena 3D:
`>= 300 vértices` **y** `>= 16 draw calls`. Separan la escena real del ruido con tres
órdenes de magnitud de margen, así que los valores exactos no son delicados. Exigir
ambas señales sí importa: una tirada larga de draws de 2 vértices tampoco es una
escena.

Cuando nada supera el umbral el resultado es "sin coincidencia", y eso es la respuesta
correcta y no un fallo: en una pantalla de título o un menú 2D no hay escena 3D que
sombrear.

### El ajuste ya está en la GUI

`FaroDepthSnapshot` dejó de ser solo una clave de `settings.xml`. Ahora es una casilla
en Ajustes → Gráficos, dentro del grupo de post-proceso Faro: *"Use a private copy of
the scene depth (experimental)"*. Es el interruptor que hay que tocar cuando SSAO/SSR
se portan mal, así que tenía que ser encontrable sin editar un archivo.

---

## 14. El flicker de SSAO: hazard de descriptor set en vuelo

Siete rondas investigando esto. La causa no estaba en la selección de profundidad, ni
en el contenido, ni en el despacho, ni en la aritmética del shader. Estaba en el
plumbing de Vulkan.

### Cómo se acotó

Por eliminación, toda con medición:

| Hipótesis | Cómo se descartó |
|---|---|
| Selección de profundidad inestable | Sonda periódica: mismo buffer 1920x1080 con 1.7–4.2M vértices en todas las ventanas, `lastUsed 0`. Cero alternancia. |
| Validez de profundidad cayéndose | 0 líneas `no scene depth buffer matched`, 0 `switched scene depth buffer`. |
| Contenido de profundidad obsoleto | Snapshot de la fase F activo; el flicker siguió. |
| SSAO saltándose frames | Contador: `GTAO ran 600/600 frames` en todas las ventanas. |
| TAA cayendo al fallback sin AA | Contador: `0/600` en todas las ventanas. |
| Historial del TAA inutilizable | Contador: `0/600`. |
| Parámetros de GTAO variables por frame | Todos constantes: intensidad de config, tabla por calidad, far plane fijo. |
| Normalización inestable `occ/wsum` | Arreglada (era un bug real), el flicker siguió. |
| **El propio pase es SSAO** | **Confirmado por el usuario**: apagar el post-proceso global lo elimina. |

### La causa

`RunFaroGtaoPass` mantenía **un único** `m_gtaoDescriptorSet` y reescribía sus
bindings 0 y 2 con `vkUpdateDescriptorSets` **en cada ejecución**, justo antes del
draw. El comentario original lo decía con todas las letras: *"bindings 0/2 rewritten
every run below"*.

Vulkan prohíbe actualizar un descriptor set mientras un command buffer ya enviado
sigue referenciándolo. Cemu mantiene varios frames en vuelo, así que el frame anterior
estaba leyendo ese mismo set cuando se sobrescribía. Lo que la GPU veía para ese frame
quedaba a merced del driver y del *timing*.

Eso explica cada una de las propiedades del síntoma:

- **Intermitente**, porque depende de si el frame previo sigue en vuelo al actualizar.
- **Invisible a toda la instrumentación de CPU**: el pase se despachaba siempre y la
  profundidad era demostrablemente estable. Solo la vista que la GPU tenía de los
  descriptores era incorrecta.
- **Anterior a esta sesión**, porque viene de cuando se escribió GTAO.
- **Frecuencia distinta con FXAA que con SMAA**: SMAA añade tres pases, lo que cambia
  la carga y el solape entre frames, y con ello la probabilidad de la carrera.
- **Desaparece al apagar el post-proceso**, porque el pase deja de grabarse.

### El arreglo

El propio Cemu nunca hace esto. Su `backbufferBlit_createDescriptorSet` **cachea** los
sets con una clave formada por las vistas a las que apuntan y escribe cada set
exactamente una vez. `GetFaroEffectDescriptorSet` hace lo mismo para los pases Faro:
clave con (vista de color, sampler, vista de profundidad, sampler), alta nueva por
combinación distinta, escritura única.

El número de combinaciones se mantiene mínimo: la entrada de color es una de las pocas
vistas intermedias, que solo se recrean al redimensionar, y el lado de profundidad es
o la vista del snapshot (una sola, fija) o la vista de profundidad de escena.

### Pendiente del mismo tipo

`RunFaroSsrPass` tiene **el mismo patrón** con sus propios sets. No se ha tocado
porque los contadores muestran que SSR no se ejecuta ni un frame (`SSR ran 0/600`),
que es un bug aparte por investigar. Cuando SSR vuelva a funcionar, hay que
convertirlo a este mismo helper antes de darlo por bueno.

### Lección de método

El síntoma se atribuyó a SSAO/SSR desde el principio y se aceptó sin verificar. La
pregunta que lo partió en dos —apagar el post-proceso global y ver si sigue— cuesta
treinta segundos y debió ser lo primero, antes de seis rondas de arreglos en la
selección de profundidad. Esos arreglos corrigieron bugs reales (un buffer de 30
vértices ganando la selección, pipelines destruidos en uso, un device lost), pero
ninguno era este.

---

## 15. Auditoría: por qué ReShade no parpadea sobre Cemu y nuestra implementación sí

Pregunta planteada: estando *dentro* del emulador, con acceso directo al buffer de
profundidad, nuestra AO debería ser mejor que ReShade, no peor. ¿Por qué parpadea?

Respuesta corta: **estar dentro del emulador solo compra mejor acceso a la
profundidad. No compra nada del algoritmo de AO, y nuestro algoritmo está a un buffer
de normales y dos pases de filtrado de distancia de MXAO.** El acceso a la
profundidad nunca fue el cuello de botella — y eso explica por qué siete rondas
arreglando la tubería de profundidad no movieron el síntoma.

Referencia auditada: `Shaders/qUINT_mxao.fx` de
[martymcmodding/qUINT](https://github.com/martymcmodding/qUINT), 705 líneas. Marty
describe MXAO como "algorítmicamente similar a GTAO y CACAO".

### Comparación de arquitectura

| | MXAO (ReShade) | Faro GTAO |
|---|---|---|
| Pases | **5** | **1** |
| Buffer de normales | sí, `MXAO_NormalTex` | **ninguno** |
| Suavizado de normales | sí, `smooth_normals()` | n/a |
| Denoise | **2 pases bilaterales** | **ninguno** |
| Patrón de ruido | dither ordenado 4×4 | hash por píxel |
| Resolución de la AO | `MXAO_GLOBAL_RENDER_SCALE` (sub-res) + upsample | resolución completa |
| Stencil para descartar píxeles | sí | no |
| Muestras | — | 36 / 64 / 144 |

Pases de MXAO, de su propia `technique`:

```
pass 1: PS_InputBufferSetup   → MXAO_ColorTex, MXAO_DepthTex, MXAO_NormalTex
pass 2: PS_StencilSetup
pass 3: PS_AmbientObscuranceHQ → CommonTex0
pass 4: PS_SpatialFilter1      → CommonTex1     (blur_filter, 4 taps, radio 0.75)
pass 5: PS_SpatialFilter2                        (blur_filter, 8 taps, radio 1/scale)
```

### Las tres diferencias que importan

**1. MXAO calcula oclusión respecto a la normal de la superficie. Nosotros no.**

MXAO reconstruye normales eligiendo, por eje, el delta que *no* cruza una
discontinuidad:

```hlsl
position_delta_x1 = lerp(position_delta_x1, position_delta_x2,
                         abs(position_delta_x1.z) > abs(position_delta_x2.z));
normal = normalize(cross(position_delta_y1, position_delta_x1));
```

luego las suaviza (`smooth_normals`), desplaza el origen de muestreo a lo largo de la
normal para evitar auto-oclusión (`position += normal * linear_depth`), y calcula el
ángulo de horizonte **relativo a la normal**:

```hlsl
float vn = dot(delta_v, normal) * rsqrt(v2);
```

Eso es lo que hace que GTAO/HBAO sea GTAO/HBAO. Lo nuestro es:

```glsl
occ += clamp((viewZ - sz) / relScale, 0.0, 1.0) * w;
```

Una diferencia de profundidad cruda a lo largo de direcciones en espacio de pantalla,
sin normal. Sin normal no hay ángulo de horizonte y no se puede distinguir "superficie
inclinada alejándose" de "algo que ocluye". En una superficie plana vista en ángulo eso
genera oclusión espuria que **depende de la dirección de muestreo**, es decir, varianza
extra por píxel.

**2. MXAO quita el ruido. Nosotros lo mostramos tal cual.**

Toda AO basada en muestreo con kernel rotado por píxel produce ruido de alta
frecuencia; es inherente al método. MXAO lo acepta y lo **elimina** con dos pases
bilaterales, ponderando cada tap por similitud de profundidad y normal
(`compute_spatial_tap_weight`). Nosotros multiplicamos la AO cruda directamente al
color. No existe ningún pase de denoise en toda la ruta — los `bilateral` que aparecen
en el archivo son del GI (desactivado) y del SSR.

**3. Y aquí está el mecanismo exacto del parpadeo.**

Nuestro ruido es `GtaoHash(gl_FragCoord.xy)`: un hash continuo de alta frecuencia,
distinto en cada píxel, **fijo en coordenadas de pantalla**. La escena se desliza por
debajo de ese patrón fijo al mover la cámara, así que **cada punto de la superficie
recibe una rotación de kernel distinta en cada frame** → una estimación ruidosa
distinta cada frame → el patrón de AO repta y chispea. Eso es exactamente un parpadeo,
es peor en geometría de alta frecuencia (hierba, follaje) y **ninguna corrección en la
tubería de profundidad puede afectarlo**.

MXAO lo evita por dos vías simultáneas: su jitter es un dither **ordenado 4×4**
(`dot(floor(vpos.xy % 4 + 0.1), float2(0.0625, 0.25)) + 0.0625`), no un hash por
píxel, así que los píxeles vecinos comparten rotación en bloques que el filtro
bilateral puede promediar de forma coherente; y luego los dos pases de filtrado
promedian sobre esos bloques.

### Qué sí aporta estar dentro del emulador

Conviene no tirar el bebé con el agua: la ventaja real existe, pero es otra.

- Acceso al buffer de profundidad **sin heurística de adivinación**. ReShade tiene que
  deducir cuál de los miles de recursos es la escena; nosotros tenemos las
  estadísticas de draw calls y vértices por buffer directamente del command processor
  (sección 2), que es información que ReShade no puede obtener.
- Podemos copiar la profundidad en el momento exacto del frame (sección 11), no solo
  al presentar.
- No dependemos de inyección ni de hooks de API.

Esas ventajas son reales y están implementadas. Simplemente no son las que arreglan el
parpadeo, porque el parpadeo no venía de la profundidad.

### Plan: fase I — reescribir GTAO con la arquitectura correcta

| Paso | Trabajo |
|---|---|
| I.1 | Pase de normales: reconstruir desde la profundidad con la selección "mejor de dos deltas" de MXAO, a un target nuevo en resolución nativa. |
| I.2 | Suavizado de normales, equivalente a `smooth_normals`. |
| I.3 | Rehacer la matemática de oclusión como horizonte relativo a la normal (`dot(delta_v, normal)`), con desplazamiento del origen a lo largo de la normal. |
| I.4 | Cambiar el jitter de hash por píxel a dither ordenado 4×4. **Hecho.** |
| I.5 | Dos pases bilaterales de denoise sobre la AO, ponderados por profundidad y normal, antes de aplicarla al color. |
| I.6 | Opcional: calcular la AO a sub-resolución y hacer el upsample en el segundo filtro, como MXAO. Recupera el coste de los pases nuevos. |

Los pasos I.4 y I.5 son los que quitan el parpadeo. I.1–I.3 son los que hacen que la
AO sea correcta en vez de una heurística de diferencias de profundidad.

### Nota sobre el arreglo de la sección 14

El hazard del descriptor set era un bug real —actualizar un descriptor set que un
command buffer en vuelo todavía referencia es comportamiento indefinido en Vulkan, y
el patrón contradecía el del propio Cemu— y había que arreglarlo. Pero no era la causa
del parpadeo. Se mantiene por correctitud, no como solución.

---

## 16. Fase I: implementación de I.4 + I.5

Implementados los dos pasos que atacan el parpadeo, sin tocar todavía la matemática
de la oclusión (I.1–I.3, el buffer de normales).

### Simplificación respecto al plan

El plan hablaba de dos pases bilaterales como MXAO. Se implementó **uno** más ancho.
Razón: el primer filtro de MXAO cumple doble función, denoise y *upsample* de su
buffer de AO a sub-resolución. Nosotros corremos a resolución completa, así que esa
segunda función no aplica. Un pase con el set de taps completo es el equivalente, y
baja el coste de 2 targets nuevos y 4 pases a **1 target y 2 pases**.

### I.4 — dither ordenado

```glsl
// antes
float rot = GtaoHash(gl_FragCoord.xy) * 3.14159265;
// ahora, tomado del jitter de MXAO
float ditherJitter = dot(floor(mod(gl_FragCoord.xy, 4.0) + 0.1), vec2(0.0625, 0.25)) + 0.0625;
float rot = ditherJitter * 3.14159265;
```

16 rotaciones en un tile 4×4, de modo que los píxeles vecinos las comparten en bloques
regulares. Eso es lo que permite al filtro promediar coherentemente en vez de mezclar
estimaciones no relacionadas. **Por sí solo no quita el parpadeo** — habilita que I.5
lo quite.

### I.5a — separar AO de color

El pase de GTAO escribía `color * ao`. Un filtro bilateral tiene que promediar
estimaciones de oclusión entre sí, y no puede una vez están fundidas en el color de la
escena. Ahora escribe solo el factor de AO (con intensidad y *distance fade* ya
incorporados, para que el filtro no necesite conocer ninguno de los dos).

Target nuevo: `m_faroAo*`, **R16_SFLOAT** de un canal a resolución del pase. Formato
fijado en lugar de heredado del swapchain porque la AO es un escalar en [0,1] y
R16_SFLOAT está en el conjunto obligatorio de Vulkan para color attachment más
muestreado con filtro lineal — sin fallback por dispositivo.

### I.5b — el pase bilateral

`s_gtao_denoise_shader_body`: binding 0 = AO, 2 = profundidad, 3 = color de escena.
Tap set de `blur_filter` de MXAO (dos anillos de cuatro a offsets de medio píxel,
elegidos para repartirse por el tile 4×4 del dither en vez de caer repetidamente en
píxeles que comparten rotación). Peso por cercanía de profundidad, con tolerancia
relativa al centro por la misma razón que el pase de AO usa una relativa: la
profundidad linealizada no tiene unidad calibrada aquí.

Bilateral y no un blur plano porque un blur plano haría halo en cada silueta.

### Notas de implementación

- `EnsureFaroAoTarget` se llama **antes** de crear cualquier pipeline en la función.
  Un cambio de tamaño destruye el target, y eso retira pipelines de la caché; sostener
  un handle a través de eso es lo que rompió la transición a pantalla completa en la
  sección 12.
- `GetFaroEffectDescriptorSet` extendido con un par vista/sampler opcional para el
  binding 3, que solo se escribe cuando se pasa — el spec lo permite mientras ningún
  pipeline enlazado lo use estáticamente, que es el mismo razonamiento que
  `swapchain_createDescriptorSetLayout` ya documenta para los bindings 2 y 3.
- Un solo bloque de uniforms sirve a los dos pases: el target de AO se crea con las
  dimensiones del pase, así que `outputResolution` también es correcto para el cálculo
  de tamaño de téxel del denoise.

### Pendiente de la fase I

I.1–I.3: buffer de normales, suavizado, y oclusión como horizonte relativo a la
normal. Eso es lo que hace que la AO sea *correcta* en lugar de una heurística de
diferencias de profundidad — y lo que elimina la oclusión espuria en superficies
planas vistas en ángulo.

---

## 17. La causa del parpadeo: el fade de distancia sobre un far plane inventado

Pista decisiva, aportada por el usuario y no por el análisis: *"estando quieto el
alcance del efecto aumenta y baja solo a la distancia"*. Ninguna de las hipótesis
anteriores podía producir eso.

### El cálculo

```glsl
float GtaoLinearize(float d) {
    float farPlane = max(faroGtaoParams.w, 1.0);   // 1000.0, inventado
    return 1.0 / max(1.0 - d * (1.0 - 1.0 / farPlane), 1e-5);
}
float distFade = 1.0 - smoothstep(30.0, 120.0, viewZ);
```

Dónde cae la banda 30→120 en profundidad cruda:

| `d` crudo | `viewZ` | `distFade` |
|---|---|---|
| 0.90 | 9.9 | 1.00 |
| 0.96 | 24.4 | 1.00 |
| **0.9676** | **30** | **1.00** |
| 0.98 | 50 | 0.88 |
| 0.99 | 91 | 0.17 |
| **0.9924** | **120** | **0.00** |

Toda la transición ocurría dentro de una franja del **2.5%** del rango de
profundidad, al extremo del fondo.

### Las dos consecuencias

1. **El efecto estaba muy recortado.** La profundidad en perspectiva está fuertemente
   concentrada al fondo: con near/far típicos, el medio campo visible ya supera
   `d=0.99`, donde el fade dejaba la AO al 17%.
2. **El alcance dependía de la proyección del juego.** Dónde cae esa franja en
   términos de mundo lo determinan los planos near/far reales, que el shader no puede
   leer. Cualquier cosa que los cambie desliza la curva completa: el graphic pack
   **"Draw Distance"** que el usuario tenía activo hace exactamente eso, y BOTW además
   ajusta la proyección por zona. De ahí que el alcance subiera y bajara solo.

### Por qué nada de lo anterior lo tocaba

La *fuerza* de la AO siempre fue invariante de escala: el test de oclusión
`(viewZ - sz) / (viewZ * relThreshold)` se reduce a `(1 - sz/viewZ) / relThreshold`,
una razón pura entre profundidades. **El fade era la única parte del pase que el far
plane inventado podía distorsionar** — y era el único sitio donde no busqué, porque el
comentario del código lo justificaba de forma convincente.

### El arreglo

```glsl
float distFade = 1.0 - smoothstep(0.995, 0.9995, depth);
```

Sobre el valor crudo. La precisión se degrada en función del valor crudo, que es donde
vive el problema real, y el valor crudo no necesita ningún far plane para
interpretarse: inmune a cambios de proyección.

### Lección de método

El comentario del código justificaba el fade con razonamiento y referencias a pruebas
previas, y lo acepté. Verificar sus números —la tabla de arriba— cuesta dos minutos y
enseña que la banda estaba en un sitio absurdo. Un comentario bien argumentado no es
evidencia de que las constantes sean correctas.

---

## 18. SSAO: implementación final (funcionando)

Confirmado por el usuario: sin parpadeo y sin parches. Esta sección documenta el
estado final y por qué cada pieza es como es, para que nadie — incluido yo en otra
sesión — deshaga algo de esto creyendo que simplifica.

### Arquitectura

Dos pases, modelados sobre ReShade MXAO (`qUINT_mxao.fx`) pero sin su buffer de
normales, que queda pendiente como fase I.1–I.3:

```
pase 1: s_gtao_shader[quality]   -> m_faroAo*        (R16_SFLOAT, 1 canal)
pase 2: s_gtao_denoise_shader    -> target del llamante (AO filtrada x color)
```

Y fuera del shader, alimentándolo:

```
selección de buffer de profundidad  (heurística tipo generic_depth, sección 2)
   -> snapshot privado del depth    (fase F, secciones 11/12)
      -> pase de AO -> pase de denoise
```

### Las cinco piezas que hacen que funcione

**1. Dither ordenado 4×4** en lugar de hash por píxel.

```glsl
float ditherJitter = dot(floor(mod(gl_FragCoord.xy, 4.0) + 0.1), vec2(0.0625, 0.25)) + 0.0625;
```

16 rotaciones en un tile, compartidas por bloques entre píxeles vecinos. Un hash por
píxel es el truco estándar **solo cuando hay un denoise detrás**; sin él, el ruido
crudo va a pantalla, y como el patrón está anclado al espacio de pantalla mientras la
escena se desliza por debajo, cada punto de superficie recibía una estimación distinta
cada frame.

**2. AO separada del color**, para que exista algo que filtrar. El pase de AO escribe
el factor (con intensidad y distance fade ya incorporados); el de denoise lo promedia
y lo multiplica. Un filtro bilateral tiene que promediar estimaciones de oclusión
entre sí, y no puede una vez están fundidas en el color.

**3. Denoise bilateral**, tap set de `blur_filter` de MXAO (dos anillos de cuatro a
offsets de medio píxel, repartidos por el tile del dither). Pesos por cercanía de
profundidad con tolerancia **relativa** al centro. Bilateral y no blur plano: un blur
plano haría halo en cada silueta.

**4. Radio de marcha escalado por profundidad.**

```glsl
float marchPx = clamp(max(faroGtaoParams.z, 1.0) * ((1.0 - depth) / 0.01), 2.0, 96.0);
```

Era una constante en píxeles, y eso hacía la AO inestable **solo a distancia**: cerca,
32 px abarcan poco mundo y los taps caen en la misma superficie; lejos, los mismos
32 px abarcan una distancia enorme, los taps caen en geometría inconexa, sus pesos se
colapsan y una o dos muestras deciden el píxel entero.

`(1 - depth)` es el factor correcto **y no necesita far plane**: para una proyección en
perspectiva `d = f/(f-n)·(1 - n/z)`, así que `(1 - d) ∝ 1/z`, que es exactamente cómo
se proyecta una longitud fija de mundo. Esto importa porque todo intento previo de
escalar por distancia pasaba por el far plane inventado de `GtaoLinearize`.

**5. Distance fade sobre profundidad cruda**, no sobre `viewZ`:

```glsl
float distFade = 1.0 - smoothstep(0.995, 0.9995, depth);
```

Sobre `viewZ` la transición caía dentro de una franja del 2.5% del rango, y su
posición en el mundo dependía de los planos near/far reales del juego — que un
graphic pack de draw distance modifica y que BOTW ajusta por zona. De ahí que el
alcance subiera y bajara solo.

### Y la pieza de fuera del shader que resultó ser la causa raíz

**Segmentación por clear, con el boundary en el CLEAR y no en el desenlace.**

La regla de copia del snapshot compara la carga de trabajo del *segmento*. El
contador se resetea en `LatteRenderTarget_applyTextureDepthClear`, enganchado
explícitamente para esto. Las dos variantes equivocadas, por orden histórico:

| Dónde se contaba | Qué pasaba |
|---|---|
| Acumulado del frame (`current`) | Solo crece dentro del frame, así que **el último desenlace siempre ganaba**. Tras un clear + pase pequeño, se capturaba el buffer recién limpiado → **fogonazos** donde la profundidad circundante es oscura, es decir con la cámara baja. |
| Segmento reseteado en cada **desenlace** | Un juego enlaza/desenlaza el mismo buffer varias veces acumulando sobre el mismo contenido. Partía la escena en trozos y solo el más pesado superaba el listón → snapshot a mitad de escena → **parches de suelo y estructuras "transparentes"**. |
| Segmento reseteado en cada **clear** ✔ | Correcto. Es el único boundary que significa "contenidos distintos", y es el que usa ReShade (`on_clear_depth_impl`). |

El listón del frame (`s_depthSnapshotBestVertices`) **no** se resetea en el clear, a
propósito: su función es terminar el frame con el segmento más pesado, así que un pase
ligero posterior a un clear no debe desplazar la escena ya capturada.

### Dos defectos del snapshot corregidos de paso

- **Apagar la casilla congelaba la imagen.** La validez es pegajosa, así que dejar de
  copiar hacía que GTAO siguiera leyendo la última copia para siempre en vez de caer
  al buffer vivo. Ahora invalida al apagarse — y con ello el A/B de la casilla sirve
  como herramienta de diagnóstico.
- **Guarda de obsolescencia**: si las copias dejan de llegar, tras dos frames cae al
  buffer vivo en lugar de congelarse en silencio.

### Parámetros vigentes

| | Low | Medium | High |
|---|---|---|---|
| Direcciones × pasos | 6×6 = 36 | 8×8 = 64 | 12×12 = 144 |
| Umbral relativo | 0.025 | 0.04 | 0.06 |
| Radio base (px a d=0.99) | 32 | 40 | 56 |

Otros: suelo del divisor `0.15 * totalWeight` (evita que una muestra suelta decida el
píxel), tolerancia del bilateral `5%` de la profundidad del centro, fade
`0.995 → 0.9995` en profundidad cruda, radio acotado a `[2, 96]` px con early-out
por debajo de 2.

### Herramienta de diagnóstico que queda disponible

**Ajustes → Gráficos → Debug view**, seis modos: Off, Raw depth (remapeado con
`pow(d, 256)` para que el fondo sea visible), AO antes del filtro, confianza de
muestreo (`wsum/total`), radio de marcha, AO después del filtro.

Esta vista es lo que resolvió el caso. Los contadores de CPU demostraron durante
rondas que el pase se despachaba siempre y que la selección de profundidad no
cambiaba — todo cierto, y todo irrelevante, porque **ninguno podía decir si los datos
de profundidad eran estables**. Un vistazo a "Raw depth" lo dijo en diez segundos.

### Lo que queda por hacer en SSAO

- **I.1–I.3**: buffer de normales, suavizado, y oclusión como horizonte relativo a la
  normal (`dot(delta_v, normal)` con desplazamiento del origen). Hoy la oclusión es
  una diferencia de profundidad cruda, lo que genera oclusión espuria en superficies
  planas vistas en ángulo. Es lo que separa esto de ser GTAO de verdad.
- El early-out de 2 px corta la AO lejana de golpe en vez de desvanecerla; si
  aparecen parches en geometría muy lejana, ese es el primer sospechoso.
- `RunFaroSsrPass` reescribe sus descriptor sets cada frame, el mismo hazard que la
  sección 14 corrigió en GTAO. No se ha tocado porque **SSR no se ejecuta ni un
  frame** (`SSR ran 0/600`), que es un bug aparte sin investigar.

## 19. El crash en pantalla de carga: `WaitDeviceIdle` con el command buffer a medio grabar

Síntoma reportado: al seleccionar una partida guardada y entrar a la pantalla de
carga, el emulador se cierra. En el log, `Exception 0xc0000005 ... in module
nvoglv64.dll` — el ICD de Vulkan de NVIDIA. Un acceso inválido dentro del driver
casi siempre significa uso inválido de la API desde nuestro lado, no un bug del
driver.

### La causa

`RunFaroMotionEstimation` llamaba a `EnsureFaroMotionTargets`, que hace
`WaitDeviceIdle()` + `DestroyFaroMotionTargets()` cuando la resolución cambia.
La pantalla de carga cambia la resolución de render, así que entraba por ahí.

Lo importante es **por qué eso es ilegal ahí y legal 200 líneas más arriba**,
porque `DrawBackbufferQuadFsr1Taa` empieza llamando a
`EnsureTaaSpatialNativeTargets` / `EnsureTaaNativeResolveTarget` /
`EnsureTaaHistoryTarget`, que hacen exactamente el mismo `WaitDeviceIdle()` sin
ningún problema.

La diferencia no es "mitad de frame" en abstracto: es **qué hay ya grabado**.

- Los `Ensure*Target` del principio corren **antes** del primer
  `vkCmdBeginRenderPass`. El command buffer está vacío.
- `RunFaroMotionEstimation` corre **después** de que el pre-pass espacial
  (FXAA/SMAA) ya grabó render passes, draws y binds de descriptor sets en
  `m_state.currentCommandBuffer`.

Y `vkDeviceWaitIdle` **sólo espera trabajo ya enviado (submitted)**; no vacía ni
envía el command buffer que se está construyendo. Así que destruir las imágenes
ahí libera memoria que comandos **ya grabados pero sin enviar** todavía
referencian. Eso es un use-after-free que el driver detecta al enviar, y revienta
dentro del ICD.

### La corrección

El mismo patrón de creación diferida que ya se aplicó al snapshot de profundidad
en la sección 12:

1. El paso que necesita el target **sólo registra el tamaño** que quiere
   (`m_faroMotionPendingWidth/Height`) y se salta a sí mismo ese frame.
2. `ProcessDeferredFaroTargets()`, llamado desde `OnFaroFrameBoundary()` en el
   swap, hace la asignación real donde nada está a medio grabar.

Coste: un frame sin reproyección tras un cambio de resolución. Precio correcto.

`EnsureFaroMotionTargets` pasó a llamarse `CreateFaroMotionTargets` para que el
nombre diga que asigna y no que "asegura" — un `Ensure*` invita a llamarlo desde
cualquier parte, que es justo lo que no se puede hacer.

### El mismo bug, latente, en GTAO

Auditando **todos** los `WaitDeviceIdle()` del renderer (no sólo el sospechoso)
apareció el tercer caso de la misma clase: `EnsureFaroAoTarget` se llama desde
dentro de `RunFaroGtaoPass`, y ese pase corre después de que RCAS ya grabó
comandos — el comentario del propio call site («la salida de RCAS de arriba sigue
fresca») lo dice. No había estallado sólo porque el tamaño del target de AO rara
vez cambia, pero en un cambio de resolución habría hecho exactamente lo mismo.
Corregido igual, y renombrado a `CreateFaroAoTarget`.

### Lección

Hubo tres instancias de esta clase de error: snapshot de profundidad, pirámide de
movimiento y target de AO. Las dos últimas las introduje yo **después** de
diagnosticar la primera. El arreglo puntual no sirvió de nada; lo que hacía falta
era buscar el patrón en todo el archivo. De ahí la regla:

> Cualquier función que haga `WaitDeviceIdle()` + destruir recursos sólo puede
> llamarse desde el límite de frame. Si asigna, se llama `Create*`, no `Ensure*`,
> y la petición desde mitad de la grabación se difiere.

Y el corolario sobre el renombrado: `Ensure*` describe una postcondición
("existe") que invita a llamarlo desde donde sea; `Create*` describe un efecto
que obliga a preguntarse cuándo es seguro.

### CORRECCIÓN a la sección 19

El diagnóstico de arriba era **incorrecto**. El arreglo se aplicó, se compiló, y
el crash siguió igual. Hechos que lo desmienten:

1. **La firma no corresponde.** `0xC0000005 READ at 0x000000F8` es un
   desreferenciado de un campo en offset `0xF8` sobre una base nula/liberada, no
   un acceso fuera de rango a una imagen destruida. Registros en el momento del
   fallo: `RAX=0 RCX=0 RDX=0 RDI=0`.
2. **`ReShade64.dll` estaba en el stack**, entre Cemu y el ICD de NVIDIA. Estaba
   en el log desde el principio y lo leí por encima. Con ReShade sacado de la
   cadena de Vulkan (`DISABLE_VK_LAYER_reshade_1=1`) el crash **se reproduce
   igual**, así que ReShade tampoco era la causa — pero haber atribuido el fallo
   a nuestro uso de Vulkan sin ver ese módulo fue un error de lectura.
3. **Los nombres de función de ese stack son basura.** `Cemu_release.pdb` es del
   26-09 y el ejecutable de hoy; por eso el crashlog mezcla
   `snd_core::AUXDRCBuffer` con llamadas de Vulkan. Cualquier lectura futura de un
   stack exige regenerar el PDB primero.

La corrección del `WaitDeviceIdle` diferido para la pirámide de movimiento sigue
siendo razonable **en sí misma**, pero no era este fallo. Y la que apliqué al
target de AO fue peor: cambié código verificado funcionando (el SSAO) basándome
en una teoría que nunca reproduje. Está revertida a inline.

**Causa probable real**, pendiente de confirmar: `GetFaroEffectDescriptorSet`
indexa su caché por el valor numérico de los handles (`(uint64)colorView ^
(uint64)depthView ^ ...`). Vulkan reutiliza valores de handle tras destruirlos,
así que una `VkImageView` recreada puede recibir el mismo valor y acertar sobre
una entrada cuyo descriptor apunta al objeto ya liberado. Eso sí produce
exactamente un `READ at 0xF8` dentro del driver. Plan en
`FARO_PLAN_TAA_C_D.md`.
