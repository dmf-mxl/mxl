// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

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

#[cfg(feature = "mxl-fabrics-ofi")]
#[test]
fn core_and_fabrics_bindings_share_handle_types() {
    let instance: mxl_sys::Instance = std::ptr::null_mut();
    let writer: mxl_sys::FlowWriter = std::ptr::null_mut();
    let reader: mxl_sys::FlowReader = std::ptr::null_mut();

    let _: mxl_sys::types::Instance = instance;
    let target_config = mxl_sys::types::FabricsTargetConfig {
        writer,
        ..Default::default()
    };
    let initiator_config = mxl_sys::types::FabricsInitiatorConfig {
        reader,
        ..Default::default()
    };

    assert!(target_config.writer.is_null());
    assert!(initiator_config.reader.is_null());
}
