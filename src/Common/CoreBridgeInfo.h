#pragma once

// Faro: minimal C ABI surface for the Tauri/cxx bridge (milestone 1 - see
// GUI_redesign_investigacion.md). Deliberately tiny and self-contained (no
// boost/fmt/wx types crossing the FFI boundary) so this first bridge only
// has to prove that Cargo can link a real CMake-built Cemu static library
// (CemuCommon) into the final Tauri binary - not that arbitrary C++ types
// are easy to bridge.
extern "C" const char* Common_GetCoreVersionString();
