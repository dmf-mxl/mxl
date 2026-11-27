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
}

#[test]
fn garbage_collect_flows_succeeds() {
    // Smoke test that the `mxlGarbageCollectFlows` FFI binding is wired
    // correctly. The C++ test suite already exercises the underlying
    // behaviour; this just confirms the safe wrapper hands the call off
    // without crashing and propagates a successful status.
    let (mxl_instance, _domain_guard) = setup_test("garbage_collect_flows");
    mxl_instance.garbage_collect_flows().unwrap();
}

#[test]
fn sleep_until_blocks_until_timestamp_then_returns() {
    // Exercises the `mxlSleepUntil` binding. A future timestamp must block at
    // least until that time is reached; a past timestamp must return promptly.
    let (mxl_instance, _domain_guard) = setup_test("sleep_until");

    let delay = Duration::from_millis(50);
    let target = mxl_instance.get_time() + delay.as_nanos() as u64;
    let started = std::time::Instant::now();
    mxl_instance.sleep_until(target);
    let elapsed = started.elapsed();

    // The MXL clock has passed the target, and real time advanced by about the
    // delay. The lower bound has slack for clock granularity; there is no tight
    // upper bound because sleep can overshoot on a loaded machine.
    assert!(mxl_instance.get_time() >= target);
    assert!(
        elapsed >= Duration::from_millis(40),
        "slept {elapsed:?}, expected at least ~40ms"
    );

    // A timestamp one second in the past must not block.
    let past = mxl_instance.get_time().saturating_sub(1_000_000_000);
    let started = std::time::Instant::now();
    mxl_instance.sleep_until(past);
    assert!(
        started.elapsed() < Duration::from_millis(500),
        "sleeping until a past timestamp should return promptly"
    );
}

#[test]
fn max_sample_lengths_are_reported_for_a_continuous_flow() {
    // Exercises the `mxlFlowReaderGetMaxReadLengthSamples` and
    // `mxlFlowWriterGetMaxWriteLengthSamples` bindings on a continuous flow.
    let (mxl_instance, _domain_guard) = setup_test("max_sample_lengths");
    let (flow_writer, flow_config_info, _was_created) = mxl_instance
        .create_flow_writer(
            read_flow_def("lib/tests/data/audio_flow.json").as_str(),
            None,
        )
        .unwrap();
    let flow_id = flow_config_info.common().id().to_string();
    let samples_writer = flow_writer.to_samples_writer().unwrap();
    let flow_reader = mxl_instance.create_flow_reader(flow_id.as_str()).unwrap();
    let samples_reader = flow_reader.to_samples_reader().unwrap();

    // A fresh writer can accept a non-zero batch (the ring buffer has capacity).
    let max_write = samples_writer.get_max_write_length_samples().unwrap();
    assert!(max_write > 0, "writer reported zero max write length");

    // The reader call succeeds and never reports more than the writer can hold.
    let max_read = samples_reader.get_max_read_length_samples().unwrap();
    assert!(
        max_read <= max_write,
        "max read {max_read} exceeds max write {max_write}"
    );
}

#[test]
fn is_flow_active_tracks_a_live_writer() {
    // Exercises the `mxlIsFlowActive` binding.
    let (mxl_instance, _domain_guard) = setup_test("is_flow_active");
    let (flow_writer, flow_config_info, _was_created) = mxl_instance
        .create_flow_writer(
            read_flow_def("lib/tests/data/v210_flow.json").as_str(),
            None,
        )
        .unwrap();
    let flow_id = flow_config_info.common().id().to_string();

    // The flow has a live writer, so it is active.
    assert!(mxl_instance.is_flow_active(flow_id.as_str()).unwrap());

    // An unknown flow id is an error, not a false result.
    let unknown = uuid::Uuid::new_v4().to_string();
    assert!(mxl_instance.is_flow_active(unknown.as_str()).is_err());

    drop(flow_writer);
}

#[test]
fn is_tmp_fs_classifies_known_paths() {
    // Exercises the `mxlIsTmpFs` binding. It classifies a filesystem path, so
    // it needs only the API handle, not an instance.
    let api = mxl::load_api(mxl::config::get_mxl_so_path()).unwrap();

    // The root filesystem is disk-backed on both Linux and macOS.
    assert!(!mxl::is_tmp_fs(&api, "/").unwrap());

    // On Linux, /dev/shm is a tmpfs mount.
    #[cfg(target_os = "linux")]
    assert!(mxl::is_tmp_fs(&api, "/dev/shm").unwrap());
}

#[test]
fn get_version_reports_a_non_empty_version() {
    // Exercises the `mxlGetVersion` binding. The version is a property of the
    // loaded library, so this needs only the API handle, not an instance.
    let api = mxl::load_api(mxl::config::get_mxl_so_path()).unwrap();
    let version = mxl::get_version(&api).unwrap();

    // The build is versioned at 1.1.0 or later, so major is at least 1 and the
    // full string is populated and starts with the numeric version.
    assert!(version.major >= 1, "unexpected major version {version:?}");
    assert!(!version.full.is_empty(), "full version string is empty");
    assert!(
        version
            .full
            .starts_with(&format!("{}.{}.", version.major, version.minor)),
        "full string {:?} does not start with {}.{}.",
        version.full,
        version.major,
        version.minor
    );
}
