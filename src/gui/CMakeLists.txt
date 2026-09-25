add_library(CemuGui INTERFACE)
target_include_directories(CemuGui INTERFACE "interface")

if(ENABLE_WXWIDGETS)
    add_subdirectory(wxgui)
    target_link_libraries(CemuGui INTERFACE CemuWxGui)
endif()

# Faro milestone 2: WindowSystem.h implementation for the Tauri shell (see
# gui/tauri/CMakeLists.txt's own comment). Built unconditionally alongside
# the wx path, but NOT wired into the CemuGui INTERFACE target above - the
# Rust/Cargo build links CemuGuiTauri directly instead of going through
# CMake's own final executable link.
add_subdirectory(tauri)
