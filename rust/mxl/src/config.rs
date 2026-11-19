// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::str::FromStr;

include!(concat!(env!("OUT_DIR"), "/constants.rs"));

fn library_file_name(name: &str) -> String {
    use std::env::consts::{DLL_PREFIX, DLL_SUFFIX};
    format!("{DLL_PREFIX}{name}{DLL_SUFFIX}")
}

#[cfg(not(feature = "mxl-not-built"))]
pub fn get_mxl_so_path() -> std::path::PathBuf {
    // The mxl-sys build script ensures that the build directory is in the library path
    // so we can just return the library name here.
    library_file_name("mxl").into()
}

#[cfg(feature = "mxl-not-built")]
pub fn get_mxl_so_path() -> std::path::PathBuf {
    std::path::PathBuf::from_str(MXL_BUILD_DIR)
        .expect("build error: 'MXL_BUILD_DIR' is invalid")
        .join("lib")
        .join(library_file_name("mxl"))
}

#[cfg(not(feature = "mxl-not-built"))]
pub fn get_mxl_fabrics_ofi_so_path() -> std::path::PathBuf {
    // The mxl-sys build script ensures that the build directory is in the library path
    // so we can just return the library name here.
    library_file_name("mxl-fabrics").into()
}

#[cfg(feature = "mxl-not-built")]
pub fn get_mxl_fabrics_ofi_so_path() -> std::path::PathBuf {
    std::path::PathBuf::from_str(MXL_BUILD_DIR)
        .expect("build error: 'MXL_BUILD_DIR' is invalid")
        .join("lib")
        .join("fabrics")
        .join("ofi")
        .join(library_file_name("mxl-fabrics"))
}

pub fn get_mxl_repo_root() -> std::path::PathBuf {
    std::path::PathBuf::from_str(MXL_REPO_ROOT).expect("build error: 'MXL_REPO_ROOT' is invalid")
}
