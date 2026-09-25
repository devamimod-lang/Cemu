#pragma once

#include <cstdint>

// Faro milestone 2 (see GUI_redesign_investigacion.md): launches a title
// into its own native GLFW window (Vulkan), entirely on its own thread -
// fire and forget, mirroring how CafeSystem::LaunchForegroundTitle already
// runs the emulation itself on a dedicated thread. Returns as soon as that
// thread is started; false only means a title window is already running
// (one at a time for this milestone), NOT that the title itself launched
// successfully - PrepareForegroundTitle failures are logged from the
// background thread, not reported back through this return value.
extern "C" bool TauriGameWindow_LaunchTitle(uint64_t titleId);
