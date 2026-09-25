use std::path::{Path, PathBuf};

fn main() {
    tauri_build::build();

    // Faro milestone 2 (see GUI_redesign_investigacion.md): build and link
    // the real Cemu core (CemuCafe + friends) plus CemuGuiTauri (our
    // WindowSystem.h implementation, replacing CemuWxGui) instead of just
    // the isolated CemuBridge from milestone 1. The exact library list below
    // was extracted directly from CemuBin.vcxproj's own Release
    // AdditionalDependencies (the ground truth for what the C++ side
    // actually needs to link) with CemuWxGui and its wx-exclusive transitive
    // deps (wxmsw33u_*, wxbase33u*, libexpatMT, pcre2-16) removed and
    // CemuGuiTauri + glfw3 added instead.
    let crate_dir = std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR not set");
    let cmake_build_dir = PathBuf::from(&crate_dir).join("..").join("build");
    let cmake_build_dir = std::fs::canonicalize(&cmake_build_dir)
        .expect("Cemu's CMake build dir (../build) not found - configure it first (see BUILD.md)");

    let cmake_targets = [
        "CemuBridge",
        "CemuAudio",
        "CemuCafe",
        "CemuCommon",
        "CemuComponents",
        "CemuConfig",
        "CemuInput",
        "CemuUtil",
        "CemuResource",
        "CemuGuiTauri",
        "imguiImpl",
        "ih264d",
        "cubeb",
        "zarchive",
    ];
    let mut cmake_args: Vec<String> = vec!["--build".into(), ".".into(), "--config".into(), "Release".into(), "--parallel".into()];
    for target in cmake_targets {
        cmake_args.push("--target".into());
        cmake_args.push(target.into());
    }
    let status = std::process::Command::new("cmake")
        .args(&cmake_args)
        .current_dir(&cmake_build_dir)
        .status()
        .expect("failed to invoke cmake - is it installed and on PATH?");
    assert!(status.success(), "cmake build of the Cemu core failed");

    // Search paths: each subproject's own Release output dir, plus vcpkg's
    // shared lib dir for third-party dependencies.
    let mut search_dirs: Vec<PathBuf> = vec![
        cmake_build_dir.join("vcpkg_installed/x64-windows-static/lib"),
        cmake_build_dir.join("src/Common/Release"),
        cmake_build_dir.join("src/Cafe/Release"),
        cmake_build_dir.join("src/Cemu/Release"),
        cmake_build_dir.join("src/config/Release"),
        cmake_build_dir.join("src/input/Release"),
        cmake_build_dir.join("src/util/Release"),
        cmake_build_dir.join("src/audio/Release"),
        cmake_build_dir.join("src/resource/Release"),
        cmake_build_dir.join("src/imgui/Release"),
        cmake_build_dir.join("src/gui/tauri/Release"),
        cmake_build_dir.join("dependencies/ih264d/Release"),
        cmake_build_dir.join("dependencies/cubeb/Release"),
        cmake_build_dir.join("dependencies/ZArchive/Release"),
    ];
    // Windows SDK/VC++ library dirs (from vcvars LIB): lets rustc resolve
    // system libs (iphlpapi, ws2_32, ...) for `cargo:rustc-link-lib` the
    // same way the MSVC linker itself does. Without these, the lib target's
    // own link fails with "could not find native static library `iphlpapi`".
    if let Ok(lib_env) = std::env::var("LIB") {
        for dir in lib_env.split(';').filter(|d| !d.is_empty()) {
            search_dirs.push(PathBuf::from(dir));
        }
    }
    for dir in &search_dirs {
        println!("cargo:rustc-link-search=native={}", dir.display());
    }

    // Cemu's own libraries (built above) + third-party static libs vcpkg
    // already built for the existing wx executable. wxmsw33u_*/wxbase33u*/
    // libexpatMT/pcre2-16 deliberately omitted - those came in transitively
    // through wxWidgets::wxWidgets (PUBLIC-linked by CemuWxGui), which this
    // build never links at all.
    let libs = [
        // Cemu's own
        "CemuBridge",
        "CemuAudio",
        "CemuCafe",
        "CemuCommon",
        "CemuComponents",
        "CemuConfig",
        "CemuInput",
        "CemuUtil",
        "CemuResource",
        "CemuGuiTauri",
        "imguiImpl",
        "ih264d",
        "cubeb",
        "zarchive",
        "glfw3",
        // vcpkg third-party
        "SDL3-static",
        "boost_program_options-vc143-mt-x64-1_88",
        "boost_container-vc143-mt-x64-1_88",
        "boost_nowide-vc143-mt-x64-1_88",
        "glslang",
        "libusb-1.0",
        "zstd",
        "pugixml",
        "libcurl",
        "libssl",
        "libcrypto",
        "zip",
        "zlib",
        "jpeg",
        "libpng16",
        "tiff",
        "nanosvgrast",
        "nanosvg",
        "libwebpdecoder",
        "libwebp",
        "libwebpdemux",
        "libwebpmux",
        "libsharpyuv",
        "glm",
        "fmt",
        // Windows system libs
        "iphlpapi",
        "imm32",
        "setupapi",
        "dinput8",
        "avrt",
        "ksuser",
        "bcrypt",
        "crypt32",
        "opengl32",
        "glu32",
        "kernel32",
        "user32",
        "gdi32",
        "gdiplus",
        "msimg32",
        "comdlg32",
        "winspool",
        "winmm",
        "shell32",
        "shlwapi",
        "comctl32",
        "ole32",
        "oleaut32",
        "uuid",
        "rpcrt4",
        "version",
        "advapi32",
        "ws2_32",
        "wininet",
        "oleacc",
        "uxtheme",
        "bthprops",
    ];
    for lib in libs {
        println!("cargo:rustc-link-lib=static={lib}");
    }

    // Empirically (see milestone 1), build-script link directives only
    // reliably reach the `cemu_milestone1_lib` library crate's own link
    // step, not the sibling `cemu-milestone1` binary target's final link
    // (same package, same build script) - force every one of the above
    // explicitly for binary targets too.
    fn find_lib_file(search_dirs: &[PathBuf], name: &str) -> Option<PathBuf> {
        for dir in search_dirs {
            let candidate = dir.join(format!("{name}.lib"));
            if candidate.exists() {
                return Some(candidate);
            }
        }
        None
    }
    for lib in libs {
        if let Some(path) = find_lib_file(&search_dirs, lib) {
            println!("cargo:rustc-link-arg-bins={}", path.display());
        }
    }
    // System libs aren't found by find_lib_file (no search_dirs entry holds
    // them - they resolve via the linker's own default lib paths), but they
    // still need forcing onto the bin target's link line the same way.
    for lib in [
        "iphlpapi", "imm32", "setupapi", "dinput8", "avrt", "ksuser", "bcrypt", "crypt32",
        "opengl32", "glu32", "kernel32", "user32", "gdi32", "gdiplus", "msimg32", "comdlg32",
        "winspool", "winmm", "shell32", "shlwapi", "comctl32", "ole32", "oleaut32", "uuid",
        "rpcrt4", "version", "advapi32", "ws2_32", "wininet", "oleacc", "uxtheme", "bthprops",
    ] {
        println!("cargo:rustc-link-arg-bins={lib}.lib");
    }

    fn rerun_if_dir_changed(dir: &Path) {
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_dir() {
                    rerun_if_dir_changed(&path);
                } else {
                    println!("cargo:rerun-if-changed={}", path.display());
                }
            }
        }
    }
    let src_dir = PathBuf::from(&crate_dir).join("..").join("src");
    rerun_if_dir_changed(&src_dir);
}
