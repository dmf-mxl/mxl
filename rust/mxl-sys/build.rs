use std::env;
use std::path::PathBuf;

#[cfg(debug_assertions)]
const BUILD_VARIANT: &str = "Linux-Clang-Debug";
#[cfg(not(debug_assertions))]
const BUILD_VARIANT: &str = "Linux-Clang-Release";

fn main() {
    let manifest_dir =
        PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("failed to get current directory"));
    let repo_root = manifest_dir.parent().unwrap().parent().unwrap();
    let build_dir = repo_root.join("build").join(BUILD_VARIANT);
    let includes_dir = repo_root.join("lib").join("include");

    let build_version_dir = build_dir.join("lib").join("include");
    let build_version_dir = build_version_dir.to_string_lossy();
    println!("cargo:include={build_version_dir}");

    let includes_dir = includes_dir.to_string_lossy();
    println!("cargo:include={includes_dir}");

    let bindings = bindgen::builder()
        .clang_arg(format!("-I{includes_dir}"))
        .clang_arg(format!("-I{build_version_dir}"))
        .header("wrapper.h")
        .derive_default(true)
        .derive_debug(true)
        .prepend_enum_name(false)
        .generate()
        .unwrap();

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Could not write bindings");
}
