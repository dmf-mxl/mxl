// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#[test]
fn there_is_bindgen_generated_code() {
    let mxl_version = mxl_sys::mxlVersionType {
        major: 3,
        minor: 2,
        bugfix: 1,
        ..Default::default()
    };

    println!("mxl_version: {:?}", mxl_version);
}
