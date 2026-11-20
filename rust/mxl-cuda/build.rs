// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::env;
use std::path::PathBuf;

/// Binds the types and the storage type name of the header of the cuda-linear backend's storage
/// type, which needs MXL's headers and not CUDA's.
fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo_root = manifest_dir.parent().unwrap().parent().unwrap();
    let backend_include = repo_root.join("lib/payload-backends/cuda-linear/include");
    let header = backend_include.join("mxl/storage/cuda_device_pointer.h");
    println!("cargo:rerun-if-changed={}", header.display());

    bindgen::builder()
        .clang_arg(format!("-I{}", repo_root.join("lib/include").display()))
        .clang_arg(format!("-I{}", backend_include.display()))
        .header(header.to_string_lossy())
        .allowlist_type("mxlCuda.*")
        .allowlist_var("MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER")
        .blocklist_function(".*")
        .derive_default(true)
        .derive_debug(true)
        .generate()
        .expect("Could not generate the CUDA storage type bindings")
        .write_to_file(PathBuf::from(env::var("OUT_DIR").unwrap()).join("bindings.rs"))
        .expect("Could not write the CUDA storage type bindings");
}
