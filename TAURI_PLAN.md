# Plan: reemplazar wxWidgets por frontend Tauri

Decisión base (ya tomada): el shell de Cemu (lista de juegos, ajustes, etc.)
pasa a Tauri (React + Tailwind, WebView nativo del SO). El núcleo C++ queda
intacto (CemuConfig, CafeTitleList, CafeSystem, renderers). Requisito duro:
**un solo .exe**, sin empaquetadores que autoextraigan (por eso Tauri y no
Electron). La ventana de juego y el shell son ventanas nativas separadas.
Investigación previa: `GUI_redesign_investigacion.md`.

## Milestone 1: bridge Tauri + núcleo C++ — ✅ HECHO Y VALIDADO

`cargo tauri build` produce un solo .exe que llama a una función C++ real
(`Common_GetCoreVersionString`) vía FFI y la muestra en UI React+Tailwind.
El build wx (CemuBin) quedó intacto.

Desviaciones reales vs. lo planeado originalmente:

- FFI `extern "C"` simple, no cxx — innecesario para `const char*`; cxx queda
  para cuando se bridgeen tipos C++ reales.
- `CemuCommon` no es aislable (su `ExceptionHandler.cpp` arrastra medio
  núcleo al linkear) — se creó `CemuBridge` como librería nueva y separada
  (`src/Common/CMakeLists.txt`), pensada para crecer.
- Cargo no propaga el link nativo del build script al binario hermano —
  se fuerza con `cargo:rustc-link-arg-bins` en `build.rs`.
- Rust necesita `-C target-feature=+crt-static`
  (`src-tauri/.cargo/config.toml`) para calzar con el CRT estático (/MT).
- El build de Vite va a `web-dist/`, NO a `dist/` (ahí hay assets de
  packaging trackeados que vite borraría).

## Milestone 2: lista real + ventana de juego GLFW — 🔄 EN CURSO

Reemplaza a wx para jugar: biblioteca real en React, click arranca el juego
en ventana nativa (Vulkan) con teclado. Sin mocks, sin delegar a CemuBin.exe.
Ajustes, graphic packs y debugger PPC quedan para después.

### Estado por pieza (verificado en el árbol)

- [x] Bootstrap del núcleo (`CafeSystemBootstrap_InitializeMinimal()` en
  `CemuCafe`): config, crypto, `CafeSystem::Initialize()`, title list,
  InputManager. Sin piezas wx. Compila.
- [x] `CemuGuiTauri`: implementa `WindowSystem.h` completo (reales las
  load-bearing, stub el resto), paralela a `CemuWxGui`, sin tocar el target
  `CemuGui` INTERFACE. Compila. Provee además `g_isGPUInitFinished` (vive en
  `main.cpp` upstream, fuera de toda librería).
- [x] Ventana GLFW (`TauriGameWindow_LaunchTitle`): hilo propio, `GLFW_NO_API`,
  HWND a `WindowInfo`, hook Win32 crudo de teclado (desambiguación L/R como
  `CemuApp::FilterEvent`), `PrepareForegroundTitle` + `LaunchForegroundTitle`,
  `glfwWaitEventsTimeout` (sin swapbuffers: presenta Vulkan directo).
  `glfw3` vía vcpkg. Compila.
- [x] C-ABI lista de juegos (`TitleListBridge_ForEachTitle`, callback con
  copia en Rust). Compila.
- [x] Rust: comandos `core_init` (OnceLock, una sola vez), `get_game_list`,
  `launch_title` + `get_core_info`. Compila.
- [x] React: pantalla de biblioteca real (grid, nombre + title id en hex,
  click-to-launch fire-and-forget). `tsc + vite build` en verde.
- [ ] **Link final Rust pendiente**: `cargo build` llega al link y pide los
  símbolos esperados de linkear CemuCafe (ya se resolvió `g_isGPUInitFinished`;
  pueden salir más rondas, es lo previsto). Dos fricciones detectadas:
  - El link `release` con LTO tarda ~50 min en laptop (Ryzen 7 5825U, poca
    RAM) — para iterar se agregó `[profile.quick]` (hereda release, sin
    LTO): `cargo build --profile=quick`. El bundle final sigue en release.
  - Smart App Control bloquea en caliente los .exe frescos del target dir
    (os error 4551, intermitente). Si persiste: exclusión de la carpeta en
    Defender. `build.rs` ya pasa `--parallel` al cmake interno.
- [ ] Verificación M2 (pendiente del link): UI muestra juegos reales;
  click abre ventana GLFW y renderiza; teclado responde; `cmake --build
  build --target CemuBin` sigue verde.

### Fuera de alcance de M2 (después)

Ajustes generales, graphic packs, debugger PPC, íconos en la lista (solo
decodificar TGA en Rust/React), ventana de pad, eventos
NotifyGameLoaded/RefreshGameList a React (hoy no-op), mando/gamepad (solo
teclado en M2).

## Notas de build (M2)

- Siempre con entorno MSVC cargado (`vcvars64.bat`) + `cargo` en PATH
  (`%USERPROFILE%\.cargo\bin`).
- Iterar: `cargo build --profile=quick` en `src-tauri/`. Bundle:
  `cargo tauri build` (usa `release` + LTO).
- El `Cemu_release.exe` de `emulators/cemu/bin/` va por otro camino
  (`cmake --build build --config Release`); no mezclar con el exe Tauri
  (`src-tauri/target/`).

## Estado del fork al escribir esto

TAA: el jitter por push-constant quedó bien entregado pero sin
reproyección producía shimmer, así que el jitter está deshabilitado en
origen hasta tener reproyección (ver commit `519f731`). No bloquea M2.
