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

    let out_path = PathBuf::from(env::var("OUT_DIR").expect("failed to get output directory"))
        .join("constants.rs");

    let data = format!(
        "pub const MXL_BUILD_DIR: &str = \"{}\";\n",
        build_dir.to_string_lossy()
    );
    std::fs::write(out_path, data).expect("Unable to write file");
}
