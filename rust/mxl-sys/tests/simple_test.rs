// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Simple smoke test to verify bindgen code generation.

/// Verifies that bindgen successfully generated Rust bindings.
///
/// This test constructs an MXL version structure to ensure the FFI types
/// are accessible and have the expected fields.
#[test]
fn there_is_bindgen_generated_code() {
    let mxl_version = mxl_sys::VersionType {
        major: 3,
        minor: 2,
        bugfix: 1,
        ..Default::default()
    };

    println!("mxl_version: {:?}", mxl_version);
}
