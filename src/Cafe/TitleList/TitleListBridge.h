#pragma once

#include <cstdint>

// Faro milestone 2 (see GUI_redesign_investigacion.md): minimal C ABI to
// expose CafeTitleList's already GUI-agnostic data (see TitleList.h) to the
// Tauri/Rust side. Callback-based rather than returning an allocated
// buffer/string across the FFI boundary - `name` is only valid for the
// duration of the call, the Rust callback must copy it (into a String)
// before returning.
extern "C" void CafeTitleListBridge_ForEachTitle(void (*callback)(uint64_t titleId, const char* name, void* userData), void* userData);
