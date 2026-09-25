#pragma once

// Faro milestone 2 (see GUI_redesign_investigacion.md): minimal, GUI-agnostic
// core bootstrap for the Tauri shell. Deliberately NOT a refactor of
// CemuApp::OnInit()/CemuCommonInit() (main.cpp) - those stay untouched so the
// existing wx build's behavior can't regress. This is a parallel, trimmed
// path covering the same core init (paths, MLC, config, crypto, title list,
// input manager) without any of the wx-specific pieces (first-run wizard,
// dark mode, tooltips, message boxes on failure - this logs and returns
// false instead). Windows-only for now, matching where this milestone is
// being validated; Linux/macOS path logic (see CemuApp.cpp's own per-OS
// DeterminePaths) is deferred to when this actually needs to run there.
extern "C" bool CafeSystemBootstrap_InitializeMinimal();
