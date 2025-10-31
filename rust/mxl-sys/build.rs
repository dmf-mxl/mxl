// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::env;
use std::path::PathBuf;

#[cfg(debug_assertions)]
const BUILD_VARIANT: &str = "Linux-Clang-Debug";
#[cfg(not(debug_assertions))]
const BUILD_VARIANT: &str = "Linux-Clang-Release";

struct BindgenSpecs {
    header: String,
    includes_dirs: Vec<String>,
}

fn get_bindgen_specs() -> BindgenSpecs {
    #[cfg(not(feature = "mxl-not-built"))]
    let header = "wrapper-with-version-h.h".to_string();
    #[cfg(feature = "mxl-not-built")]
    let header = "wrapper-without-version-h.h".to_string();

    let manifest_dir =
        PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("failed to get current directory"));
    let repo_root = manifest_dir.parent().unwrap().parent().unwrap();
    let mut includes_dirs = vec![
        repo_root
            .join("lib")
            .join("include")
            .to_string_lossy()
            .to_string(),
    ];
    if cfg!(not(feature = "mxl-not-built")) {
        let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
        let build_version_dir = out_dir.join("include").to_string_lossy().to_string();

        includes_dirs.push(build_version_dir);

        // Rebuild if any file in lib/ changes
        let lib_root = repo_root.join("lib");
        println!("cargo:rerun-if-changed={}", lib_root.display());

        let dst = cmake::Config::new(repo_root)
            .generator("Ninja")
            .configure_arg("--preset")
            .configure_arg(BUILD_VARIANT)
            .configure_arg("-B")
            .configure_arg(out_dir.join("build"))
            .define("BUILD_DOCS", "OFF")
            .define("BUILD_TESTS", "OFF")
            .define("BUILD_TOOLS", "OFF")
            .build();

        println!("cargo:rustc-link-search={}", dst.join("lib").display());
        println!("cargo:rustc-link-lib=mxl");
    }

    BindgenSpecs {
        header,
        includes_dirs,
    }
}

fn main() {
    let bindgen_specs = get_bindgen_specs();
    for include_dir in &bindgen_specs.includes_dirs {
        println!("cargo:include={include_dir}");
    }

    let bindings = bindgen::builder()
        .clang_args(
            bindgen_specs
                .includes_dirs
                .iter()
                .map(|dir| format!("-I{dir}")),
        )
        .header(bindgen_specs.header)
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
