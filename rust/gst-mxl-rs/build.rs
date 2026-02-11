//! Build Script for gst-mxl-rs
//!
//! This build script runs during compilation and generates version information
//! for the GStreamer plugin using `gst_plugin_version_helper`.
//!
//! ## Generated Environment Variables
//! - `COMMIT_ID`: Git commit hash (for version string)
//! - `BUILD_REL_DATE`: Build date (for plugin metadata)
//!
//! These are accessed in lib.rs via `env!("COMMIT_ID")` and used in the
//! gst::plugin_define! macro.

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/// Main build function.
///
/// Calls the gst_plugin_version_helper to generate version info from git.
fn main() {
    gst_plugin_version_helper::info()
}
