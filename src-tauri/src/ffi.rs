//! Faro milestone 1+2 bridge (see GUI_redesign_investigacion.md): minimal
//! `extern "C"` links against the real, CMake-built Cemu libraries (see
//! build.rs). Deliberately hand-rolled FFI, not `cxx` - every bridged
//! function uses C-compatible types only (bool, uint64_t, const char*,
//! function pointers), so cxx/autocxx would add nothing but build weight.
//! That changes the day a milestone needs real C++ types across the
//! boundary.

use serde::Serialize;
use std::ffi::CStr;
use std::os::raw::{c_char, c_void};
use std::sync::OnceLock;

// `#[link]` here (not just build.rs's `cargo:rustc-link-lib`) so the
// requirement is embedded in this crate's own metadata - main.rs links the
// `cemu_milestone1_lib` rlib from the SAME package/build-script invocation,
// and empirically the build-script-only directive didn't propagate to that
// sibling binary target's link step, only to this library's own.
#[link(name = "CemuBridge", kind = "static")]
unsafe extern "C" {
    fn Common_GetCoreVersionString() -> *const c_char;
}

#[link(name = "CemuCafe", kind = "static")]
unsafe extern "C" {
    fn CafeSystemBootstrap_InitializeMinimal() -> bool;
    fn CafeTitleListBridge_ForEachTitle(
        callback: unsafe extern "C" fn(title_id: u64, name: *const c_char, user_data: *mut c_void),
        user_data: *mut c_void,
    );
}

#[link(name = "CemuGuiTauri", kind = "static")]
unsafe extern "C" {
    fn TauriGameWindow_LaunchTitle(title_id: u64) -> bool;
}

pub fn get_core_version_string() -> String {
    // Safety: Common_GetCoreVersionString returns a pointer to a static,
    // null-terminated string literal (BUILD_VERSION_WITH_NAME_STRING) - it's
    // never null and lives for the whole process lifetime.
    unsafe { CStr::from_ptr(Common_GetCoreVersionString()) }
        .to_string_lossy()
        .into_owned()
}

/// Milestone 2: one-shot core init (CafeSystemBootstrap_InitializeMinimal).
/// Guarded process-wide - React StrictMode (dev) and retries must never run
/// the C++ init twice.
pub fn core_init() -> bool {
    static DONE: OnceLock<bool> = OnceLock::new();
    *DONE.get_or_init(|| unsafe { CafeSystemBootstrap_InitializeMinimal() })
}

#[derive(Serialize, Clone)]
pub struct GameEntry {
    pub id: u64,
    pub name: String,
}

struct Collector {
    entries: Vec<GameEntry>,
}

// Runs on the C++ side's thread, once per title. Must not panic across the
// FFI boundary - every fallible step degrades to skipping the entry.
unsafe extern "C" fn collect_one(title_id: u64, name: *const c_char, user_data: *mut c_void) {
    if name.is_null() || user_data.is_null() {
        return;
    }
    // Safety: user_data is the &mut Collector we passed below, alive for the
    // whole ForEachTitle call; name is valid for the duration of the call
    // (TitleListBridge contract) and we copy it into a String immediately.
    let collector = unsafe { &mut *(user_data as *mut Collector) };
    let name = unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned();
    collector.entries.push(GameEntry { id: title_id, name });
}

/// Milestone 2: real game list from CafeTitleList (already refreshed by
/// core_init). Empty vec = zero titles found, not an error.
pub fn list_titles() -> Vec<GameEntry> {
    let mut collector = Collector { entries: Vec::new() };
    unsafe {
        CafeTitleListBridge_ForEachTitle(
            collect_one,
            &mut collector as *mut Collector as *mut c_void,
        );
    }
    collector.entries
}

/// Milestone 2: fire-and-forget into TauriGameWindow_LaunchTitle (spawns the
/// GLFW window + emulation on its own C++ thread). False = a title window
/// is already running.
pub fn launch_title(title_id: u64) -> bool {
    unsafe { TauriGameWindow_LaunchTitle(title_id) }
}
