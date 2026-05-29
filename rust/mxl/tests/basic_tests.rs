// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/// Tests of the basic low level synchronous API.
///
/// The tests now require an MXL library of a specific name to be present in the system. This should
/// change in the future. For now, feel free to just edit the path to your library.
mod common;

use std::time::Duration;

use common::{read_flow_def, setup_test};
use mxl::{OwnedGrainData, OwnedSamplesData};
use tracing::info;

#[test]
fn basic_mxl_grain_writing_reading() {
    let (mxl_instance, _domain_guard) = setup_test("grains");
    let (flow_writer, flow_config_info, was_created) = mxl_instance
        .create_flow_writer(
            read_flow_def("lib/tests/data/v210_flow.json").as_str(),
            None,
        )
        .unwrap();
    assert!(was_created);
    let flow_id = flow_config_info.common().id().to_string();
    let grain_writer = flow_writer.to_grain_writer().unwrap();
    let flow_reader = mxl_instance.create_flow_reader(flow_id.as_str()).unwrap();
    let grain_reader = flow_reader.to_grain_reader().unwrap();
    let rate = flow_config_info.common().grain_rate().unwrap();
    let current_index = mxl_instance.get_current_index(&rate);
    let grain_write_access = grain_writer.open_grain(current_index).unwrap();
    let total_slices = grain_write_access.total_slices();
    grain_write_access.commit(total_slices).unwrap();
    let grain_data = grain_reader
        .get_complete_grain(current_index, Duration::from_secs(5))
        .unwrap();
    let grain_data: OwnedGrainData = grain_data.into();
    info!("Grain data len: {:?}", grain_data.payload.len());
    grain_reader.destroy().unwrap();
    grain_writer.destroy().unwrap();
    mxl_instance.destroy().unwrap();
}

#[test]
fn basic_mxl_samples_writing_reading() {
    let (mxl_instance, _domain_guard) = setup_test("samples");
    let (flow_writer, flow_config_info, was_created) = mxl_instance
        .create_flow_writer(
            read_flow_def("lib/tests/data/audio_flow.json").as_str(),
            None,
        )
        .unwrap();
    assert!(was_created);
    let flow_id = flow_config_info.common().id().to_string();
    let samples_writer = flow_writer.to_samples_writer().unwrap();
    let flow_reader = mxl_instance.create_flow_reader(flow_id.as_str()).unwrap();
    let samples_reader = flow_reader.to_samples_reader().unwrap();
    let rate = flow_config_info.common().sample_rate().unwrap();
    let current_index = mxl_instance.get_current_index(&rate);
    let samples_write_access = samples_writer.open_samples(current_index, 42).unwrap();
    samples_write_access.commit().unwrap();
    let samples_data = samples_reader
        .get_samples(current_index, 42, Duration::from_secs(5))
        .unwrap();
    let samples_data: OwnedSamplesData = samples_data.into();
    info!(
        "Samples data contains {} channels(s), channel 0 has {} byte(s).",
        samples_data.payload.len(),
        samples_data.payload[0].len()
    );
    samples_reader.destroy().unwrap();
    samples_writer.destroy().unwrap();
    mxl_instance.destroy().unwrap();
}

#[test]
fn get_flow_def() {
    let (mxl_instance, _domain_guard) = setup_test("flow_def");
    let flow_def = read_flow_def("lib/tests/data/v210_flow.json");
    let (flow_writer, flow_info, was_created) = mxl_instance
        .create_flow_writer(flow_def.as_str(), None)
        .unwrap();
    assert!(was_created);
    let flow_id = flow_info.common().id().to_string();
    let retrieved_flow_def = mxl_instance.get_flow_def(flow_id.as_str()).unwrap();
    assert_eq!(flow_def, retrieved_flow_def);
    drop(flow_writer);
    mxl_instance.destroy().unwrap();
}

#[test]
fn garbage_collect_flows_succeeds() {
    // Smoke test that the `mxlGarbageCollectFlows` FFI binding is wired
    // correctly. The C++ test suite already exercises the underlying
    // behaviour; this just confirms the safe wrapper hands the call off
    // without crashing and propagates a successful status.
    let (mxl_instance, _domain_guard) = setup_test("garbage_collect_flows");
    mxl_instance.garbage_collect_flows().unwrap();
    mxl_instance.destroy().unwrap();
}
