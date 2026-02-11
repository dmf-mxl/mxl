// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Build-time configuration and path resolution for the MXL library.
//!
//! This module provides helper functions to locate the MXL shared library
//! and repository root based on compile-time build settings.

use std::str::FromStr;

// Build script generates constants.rs with MXL_REPO_ROOT and MXL_BUILD_DIR
include!(concat!(env!("OUT_DIR"), "/constants.rs"));

/// Returns the path to the MXL shared library (`libmxl.so`).
///
/// When the crate is built normally (without `mxl-not-built` feature), this
/// returns just the library name `"libmxl.so"` because the build script adds
/// the build directory to the linker search path.
///
/// This function is used by examples and tests to locate the library at runtime.
///
/// # Returns
///
/// Path to `libmxl.so` suitable for passing to [`crate::load_api`].
///
/// # Examples
///
/// ```no_run
/// use mxl::config::get_mxl_so_path;
/// use mxl::load_api;
///
/// # fn main() -> Result<(), mxl::Error> {
/// let api = load_api(get_mxl_so_path())?;
/// # Ok(())
/// # }
/// ```
#[cfg(not(feature = "mxl-not-built"))]
pub fn get_mxl_so_path() -> std::path::PathBuf {
    // The mxl-sys build script ensures that the build directory is in the library path
    // so we can just return the library name here.
    "libmxl.so".into()
}

/// Returns the full path to the MXL shared library when using a pre-built library.
///
/// This variant is used when the `mxl-not-built` feature is enabled, pointing
/// directly to the library in the configured build directory.
///
/// # Returns
///
/// Absolute path to `libmxl.so` in the build output directory.
///
/// # Panics
///
/// Panics if `MXL_BUILD_DIR` was set to an invalid path at build time.
#[cfg(feature = "mxl-not-built")]
pub fn get_mxl_so_path() -> std::path::PathBuf {
    std::path::PathBuf::from_str(MXL_BUILD_DIR)
        .expect("build error: 'MXL_BUILD_DIR' is invalid")
        .join("lib")
        .join("libmxl.so")
}

/// Returns the root directory of the MXL repository.
///
/// This is primarily used by tests to locate test data files (e.g., flow
/// definition JSON files) relative to the repository structure.
///
/// # Returns
///
/// Absolute path to the MXL repository root.
///
/// # Panics
///
/// Panics if `MXL_REPO_ROOT` was set to an invalid path at build time.
///
/// # Examples
///
/// ```no_run
/// use mxl::config::get_mxl_repo_root;
///
/// let flow_def_path = get_mxl_repo_root()
///     .join("lib/tests/data/v210_flow.json");
/// ```
pub fn get_mxl_repo_root() -> std::path::PathBuf {
    std::path::PathBuf::from_str(MXL_REPO_ROOT).expect("build error: 'MXL_REPO_ROOT' is invalid")
}
