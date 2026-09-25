find_package(glfw3 CONFIG REQUIRED)

add_library(CemuGuiTauri STATIC
  TauriWindowSystem.cpp
  TauriGameWindow.h
  TauriGameWindow.cpp
)

target_include_directories(CemuGuiTauri PUBLIC "../")
set_property(TARGET CemuGuiTauri PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(CemuGuiTauri PRIVATE
  CemuCommon
  CemuCafe
  glfw
)
