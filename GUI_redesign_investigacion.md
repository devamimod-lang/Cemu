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
