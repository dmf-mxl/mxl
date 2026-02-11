// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Build script for `mxl-sys` FFI crate.
//!
//! This script:
//! 1. Optionally builds the MXL C library from source using CMake
//! 2. Generates Rust FFI bindings using `bindgen`
//! 3. Configures dynamic library loading via `libloading`
//!
//! The build process respects the `mxl-not-built` feature to skip building MXL
//! when using a pre-existing library.

use bindgen::callbacks::ParseCallbacks;
use std::env;
use std::path::PathBuf;

/// CMake build variant based on debug/release mode.
#[cfg(debug_assertions)]
const BUILD_VARIANT: &str = "Linux-Clang-Debug";
#[cfg(not(debug_assertions))]
const BUILD_VARIANT: &str = "Linux-Clang-Release";

/// Bindgen configuration (header file and include directories).
struct BindgenSpecs {
    header: String,
    includes_dirs: Vec<String>,
}

/// Determines bindgen configuration and optionally builds MXL from source.
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

/// Main build entry point.
fn main() {
    let bindgen_specs = get_bindgen_specs();
    // Emit include directories for downstream crates
    for include_dir in &bindgen_specs.includes_dirs {
        println!("cargo:include={include_dir}");
    }

    // Generate Rust FFI bindings from C headers
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
        .dynamic_library_name("libmxl")
        .dynamic_link_require_all(true)
        .parse_callbacks(Box::new(CB))
        .generate()
        .unwrap();

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Could not write bindings");
}

/// Custom bindgen callback for name transformations.
#[derive(Debug)]
struct CB;

impl ParseCallbacks for CB {
    /// Transforms C names to idiomatic Rust names.
    ///
    /// - Functions: `mxlFooBar` → `foo_bar`
    /// - Types: `mxlFooBar` → `FooBar`
    fn item_name(&self, item_info: bindgen::callbacks::ItemInfo) -> Option<String> {
        match item_info.kind {
            bindgen::callbacks::ItemKind::Function => {
                Some(to_snake_case(&item_info.name.replace("mxl", "")))
            }

            bindgen::callbacks::ItemKind::Type => Some(item_info.name.replace("mxl", "")),

            _ => None,
        }
    }
}

/// Converts CamelCase to snake_case for function names.
///
/// Example: `CreateInstance` → `create_instance`
fn to_snake_case(s: &str) -> String {
    let mut out = String::new();

    for c in s.chars() {
        if c.is_uppercase() {
            if !out.is_empty() {
                out.push('_');
            }
            out.push(c.to_ascii_lowercase());
        } else {
            out.push(c);
        }
    }

    out
}
