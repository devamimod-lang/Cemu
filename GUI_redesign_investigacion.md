# Investigación: reemplazar wxWidgets en Cemu por una GUI moderna

Investigación completa antes de escribir código, como pediste. Resumen directo:
**esto es mucho más grande de lo que probablemente estás imaginando** - no es un
lavado de cara, es reconstruir buena parte de lo que wxWidgets da gratis hoy.

## 1. El tamaño real del problema

**120 archivos, 32,177 líneas** bajo `src/gui/wxgui/`. No es una capa delgada -
tiene adentro un **debugger completo de PPC con dibujo custom** (disassembly,
registros, memoria, breakpoints, símbolos - ~2,800 líneas de controles wx
dibujados a mano) y una **lista virtual custom** para la biblioteca de juegos
(`wxGameList.cpp`, 1,696 líneas, con íconos, columnas, orden, menús contextuales).

Para dimensionar: esto es comparable en tamaño al recompilador de shaders de
Latte - uno de los subsistemas más grandes que ya existe en Cemu. Mi lectura
honesta: **no es un proyecto de semanas ni de un par de meses** - es del orden de
un trimestre o más, siendo el rewrite de UI más grande que Cemu haya hecho.

## 2. La buena noticia: el acoplamiento es limpio

Esto SÍ es una sorpresa positiva. Busqué referencias a wx fuera de
`src/gui/wxgui/` en TODO el código fuente y encontré exactamente **una** (un
comentario, no código real). `CemuConfig.h/.cpp` no tiene ninguna dependencia de
wx. Y lo más importante: ya existe una interfaz limpia y agnóstica de toolkit -
`src/gui/interface/WindowSystem.h` - que el núcleo de emulación usa sin saber
nada de wx. `wxWindowSystem.cpp` es la ÚNICA implementación de esa interfaz.

Conclusión: **reemplazar wx es "cambiar una capa", no "desenredar wx de todos
lados"**. Eso reduce el riesgo real del proyecto, aunque no su tamaño.

## 3. Lo que wx da gratis y habría que reconstruir

Acá está el trabajo real escondido. wx no es solo widgets - es:
- **Localización/idiomas**: Cemu usa `wxTranslations`/gettext (`_("...")`) para
  todos sus textos traducidos.
- **Diálogos nativos**: file/folder pickers y clipboard - 146 usos en 21
  archivos (`GeneralSettings2.cpp` sola tiene 12, `MainWindow.cpp` tiene 27).
- **Message boxes** nativos.
- **Puente de threads a UI**: 17 puntos distintos donde un hilo de background
  (descarga, compilación de shaders, breakpoint del emulador) manda una
  actualización seguibleal hilo de UI vía `wxQueueEvent`/`CallAfter`. Cada uno de
  estos necesita un reemplazo funcional.

Ninguna herramienta moderna te da todo esto "gratis" - hay que reconstruirlo.

## 4. Lo que ya tenemos a favor: Dear ImGui ya está integrado y probado

Esto es clave. Cemu YA usa Dear ImGui (MIT, ya enlazado) para su overlay
in-game - pero no es solo un contador de FPS de solo lectura. El teclado en
pantalla emulado de Wii U (`swkbd.cpp`) y el diálogo de errores (`erreula.cpp`)
son **UI interactiva real hecha en ImGui** - botones, texto, estado -
dibujados directamente sobre el mismo swapchain del juego. Cemu ya demostró que
ImGui puede sostener diálogos reales, no solo texto flotante.

**El hueco real**: todo esto solo corre mientras hay un juego activo con
superficie de render. No existe hoy ningún camino que mantenga un renderer vivo
y dibuje ImGui **sin juego cargado** - la lista de juegos, ajustes y el
debugger son 100% wx hoy. Ese "render loop solo-menú" es la primera pieza de
infraestructura nueva que haría falta, y de ahí depende todo lo demás.

## 5. Tres opciones reales, con el obstáculo concreto de cada una

| Opción | A favor | El obstáculo real en ESTE código |
|---|---|---|
| **Extender Dear ImGui** | Ya enlazado, MIT, ya probado interactivo (swkbd/erreula) | No existe el "render loop sin juego" - hay que construirlo desde cero, más diálogos de archivo nativos (ImGui no trae eso) |
| **Qt** | Modelo retenido como wx - portar widget por widget es más mecánico, encaja mejor con el debugger de controles custom | Qt no existe en `vcpkg.json`/CMake hoy - dependencia nueva con implicaciones de licencia LGPL en los 3 sistemas operativos; TODO el embedding de la superficie de render (`VulkanCanvas : public wxWindow`) hay que reescribirlo contra las APIs nativas de Qt |
| **SDL3 + ImGui** | SDL3 ya está en el árbol de dependencias | Solo está usado para audio/input hoy, no para ventanas/GUI - hay que escribir un `WindowSystem` nuevo de cero igual, más reimplementar diálogos/clipboard/idiomas |

## Mi recomendación

Dado que esto es un proyecto grande sin importar qué toolkit se elija, y que
Dear ImGui ya tiene la única infraestructura PROBADA (no solo teórica) de
UI interactiva en este código, la ruta de menor riesgo es:

1. Construir primero el "render loop solo-menú" (ImGui sin juego cargado) como
   pieza aislada - es la base de todo lo demás y se puede probar sola.
2. Migrar primero los diálogos SIMPLES (`GameProfileWindow`, `GameUpdateWindow`,
   `GettingStartedDialog` - formularios chicos) para validar el patrón antes de
   tocar algo grande.
3. Dejar el debugger de PPC (el más grande y con más dibujo custom) para el
   final, o directamente fuera de alcance en una primera etapa - es lo que
   menos ve un usuario normal y lo que más cuesta portar.
4. La lista de juegos (`wxGameList.cpp`) y el diálogo de ajustes grande
   (`GeneralSettings2.cpp`) son el verdadero "producto visible" que justifica
   el esfuerzo - priorizarlos después del paso 1, antes que el debugger.

No recomiendo Qt a menos que la prioridad sea portar el debugger fielmente -
para todo lo demás, aprovechar lo que ya está probado (ImGui) es menos riesgo
que sumar una dependencia nueva con implicaciones de licencia.

¿Querés que arranque por el paso 1 (el render loop de ImGui sin juego cargado),
o preferís revisar esto primero y decidir el orden?

## Addendum: Opción 4 - Electron como shell, ventana de juego nativa separada

Planteo del usuario: no embeber Electron dentro del emulador, ni embeber la
superficie de render del juego dentro de una ventana de Electron - dos procesos
genuinamente separados. El shell (lista de juegos, ajustes, debugger) pasa a
ser una app Electron; cuando se lanza un juego, la ventana de render sigue
siendo una ventana nativa aparte, exactamente como hoy, solo que orquestada por
comandos vía IPC en lugar de por un click de wx directamente.

### Por qué esto reduce el problema real, no solo lo esconde

La sección 4 de arriba identificó el hueco concreto: "no existe hoy ningún
camino que mantenga un renderer vivo y dibuje UI sin juego cargado". Las 3
opciones originales (ImGui/Qt/SDL3) todas necesitan construir ESE render loop
desde cero. Esta cuarta opción lo evita por completo: el shell de Electron no
necesita dibujar nada relacionado a render 3D - solo necesita mostrar HTML/CSS
normal, que es lo que Electron hace mejor que cualquiera de las otras 3
opciones. La ventana de juego sigue siendo el mismo tipo de ventana nativa
aislada que ya existe hoy (ver abajo), sin tocar su arquitectura de fondo.

### Lo que ya existe en el código y hace esto viable

Revisé el acoplamiento real de la superficie de render, no solo el shell:

- `src/gui/wxgui/canvas/IRenderCanvas.h` - clase base abstracta para las 3
  superficies de render (`VulkanCanvas`, `OpenGLCanvas`, `MetalCanvas`). Su
  contrato real es un solo bool (`m_is_main_window`) - el include de wx que
  trae es vestigial, no hay tipos de wx en la interfaz misma.
- `VulkanCanvas : public IRenderCanvas, public wxWindow` (`VulkanCanvas.h:10`)
  - la parte específica de wx es SOLO la creación de la ventana nativa y el
    bombeo de eventos (resize/paint), no la lógica de Vulkan en sí.
- `src/gui/interface/WindowSystem.h` - ya es 100% agnóstico de toolkit (el
  mismo hallazgo de la sección 2, confirmado de nuevo acá): `WindowHandleInfo`
  expone handles nativos crudos (X11/Wayland/Cocoa/Windows) mediante `void*`,
  no wx. `wxWindowSystem.cpp` es la única implementación existente.

Conclusión: reemplazar la ventana de juego no es "reescribir el renderer" - es
escribir UNA clase nueva por backend (`VulkanCanvasNative`, etc.) que crea una
ventana nativa cruda (candidato obvio: GLFW - ya ampliamente usado para esto
exacto en apps gráficas, multiplataforma, sin dependencia de wx) y una nueva
implementación de `WindowSystem` que llene esos mismos `WindowHandleInfo` con
los handles de esa ventana nativa. Bounded y acotado, no un rediseño del
renderer.

### El precedente real: Streamlabs Desktop

Streamlabs Desktop (antes "Streamlabs OBS") hace exactamente este patrón en
producción, a escala grande: Electron como shell de UI, con el motor nativo de
OBS (`libobs`, C++) corriendo por su cuenta. Arquitectura documentada
públicamente:
- **Worker Window**: ventana invisible persistente que corre toda la capa de
  servicios; es el único lugar desde donde se accede a OBS directamente.
- **Main Window**: la UI visible, habla con el Worker Window por IPC.
- El acceso nativo se hace vía **`obs-studio-node`** - un addon nativo de
  Node.js (N-API/node-addon-api) que expone la API de libOBS directamente
  dentro del proceso de Node de Electron, sin un servidor de red intermedio.

Esto es un dato importante para nuestra elección de transporte IPC (ver
abajo): Streamlabs no usa sockets/HTTP para el puente principal, usa un addon
nativo compilado. Referencias: [Streamlabs Desktop - Application
Architecture](https://github.com/streamlabs/desktop/wiki/Application-Architecture),
[obs-studio-node](https://github.com/streamlabs/obs-studio-node).

### Costo real que sí hay que asumir (para ser honesto, no vender esto de más)

- **Peso**: Electron embebe Chromium + Node.js completos. Streamlabs es
  precisamente el ejemplo público más citado de la crítica a este approach -
  "un proceso de Chromium persistente corriendo junto al motor de streaming"
  (ver hilo de Hacker News abajo) - RAM y tamaño de instalación notablemente
  mayores que wx. Para un emulador donde el rendimiento y el tamaño de
  descarga importan, esto es un trade-off real, no cosmético.
- **Empaquetado**: Cemu hoy distribuye un binario nativo único (exe/AppImage/
  dmg). Sumar Electron cambia esa distribución a "runtime de Electron +
  binario nativo del núcleo + addon nativo", con su propio empaquetador
  (electron-builder u similar) y consideraciones de firma de código separadas
  por plataforma.
- **Ventana de juego nueva de todos modos**: como se explicó arriba, esto NO
  se evita del todo - hay que escribir la implementación nativa del canvas de
  render (chica en alcance comparado con el resto, pero real).
- **Diseño del puente**: dos caminos razonables, no hay que reinventar de
  cero en ninguno de los dos, porque Electron y el ecosistema Node ya traen
  todo lo que un `WindowSystem` de wx daba gratis (diálogos nativos vía
  `dialog` de Electron, portapapeles vía `clipboard`, i18n vía librerías
  estándar de Node como `i18next`):
  1. **Addon nativo N-API** (patrón `obs-studio-node`): el núcleo de Cemu se
     compila como librería enlazada directamente al proceso de Node de
     Electron. Más rápido, sin proceso de por medio, pero acopla el ciclo de
     vida del núcleo al de Electron.
  2. **Servidor local + cliente** (WebSocket/named pipe): el binario de Cemu
     corre su propio proceso, expone un API local; Electron es un cliente más.
     Más desacoplado (el núcleo puede correr sin Electron para debugging/CLI),
     pero hay que diseñar y versionar ese protocolo a mano.

### Recomendación revisada

Dado el precedente de Streamlabs y el hallazgo de que `IRenderCanvas`/
`WindowSystem.h` ya están limpios, esta opción es **más realista que las 3
originales** para llegar rápido a una UI moderna vendible (Electron da HTML/
CSS/frameworks web modernos gratis, sin tener que construir un sistema de
theming/layout desde cero como requeriría ImGui). El costo de peso/RAM es el
trade-off consciente a aceptar. Sugiero el servidor local + cliente (opción 2
de arriba) sobre el addon N-API: mantiene el núcleo de Cemu ejecutable e
inspeccionable independientemente de Electron (útil para debugging y no ata
el ciclo de vida del emulador al de Chromium), a costa de tener que mantener
un protocolo propio en vez de bindings directos.

Próximo paso natural: un prototipo aislado y chico - una ventana GLFW vacía
más un servidor local mínimo (ping/pong) hablado desde una app Electron de
"hola mundo" - para validar el puente antes de mover cualquier pantalla real.

Referencias: [Streamlabs Desktop wiki - Application
Architecture](https://github.com/streamlabs/desktop/wiki/Application-Architecture),
[obs-studio-node (GitHub)](https://github.com/streamlabs/obs-studio-node),
[Hacker News - crítica al overhead de
Electron+OBS](https://news.ycombinator.com/item?id=28276872),
[Electron IPC docs](https://www.electronjs.org/docs/latest/tutorial/ipc).
