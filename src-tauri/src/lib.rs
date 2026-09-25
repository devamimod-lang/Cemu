mod ffi;

use ffi::GameEntry;

// Faro milestone 1: this is the whole point of the prototype - the string
// returned here comes from the real, CMake-built Cemu core (see ffi.rs),
// not a mock, proving the Cargo+CMake+FFI+Tauri+React chain works end to end.
#[tauri::command]
fn get_core_info() -> String {
    ffi::get_core_version_string()
}

// Faro milestone 2: one-shot core bootstrap (config, MLC, crypto, title
// list, input). The React library screen calls this once on mount, before
// get_game_list.
#[tauri::command]
fn core_init() -> bool {
    ffi::core_init()
}

// Faro milestone 2: real detected-title list (same source wxGameList shows).
#[tauri::command]
fn get_game_list() -> Vec<GameEntry> {
    ffi::list_titles()
}

// Faro milestone 2: launch a title into its own native GLFW window.
// Fire-and-forget - false only means another title window is already open.
#[tauri::command]
fn launch_title(id: u64) -> bool {
    ffi::launch_title(id)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![
            get_core_info,
            core_init,
            get_game_list,
            launch_title
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
