// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/// Tests of the basic low level synchronous API.
///
/// The tests now require an MXL library of a specific name to be present in the system. This should
/// change in the future. For now, feel free to just edit the path to your library.
use std::time::Duration;

use mxl::{MxlInstance, OwnedGrainData, OwnedSamplesData, config::get_mxl_so_path};
use tracing::info;

static LOG_ONCE: std::sync::Once = std::sync::Once::new();

struct TestDomainGuard {
    dir: std::path::PathBuf,
}

impl TestDomainGuard {
    fn new(test: &str) -> Self {
        let dir = std::path::PathBuf::from(format!(
            "/dev/shm/mxl_rust_unit_tests_domain_{}_{}",
            test,
            uuid::Uuid::new_v4()
        ));
        std::fs::create_dir_all(dir.as_path()).unwrap_or_else(|_| {
            panic!(
                "Failed to create test domain directory \"{}\".",
                dir.display()
            )
        });
        Self { dir }
    }

    fn domain(&self) -> String {
        self.dir.to_string_lossy().to_string()
    }
}

impl Drop for TestDomainGuard {
    fn drop(&mut self) {
        std::fs::remove_dir_all(self.dir.as_path()).unwrap_or_else(|_| {
            panic!(
                "Failed to remove test domain directory \"{}\".",
                self.dir.display()
            )
        });
    }
}

fn setup_test(test: &str) -> (MxlInstance, TestDomainGuard) {
    // Set up the logging to use the RUST_LOG environment variable and if not present, print INFO
    // and higher.
    LOG_ONCE.call_once(|| {
        tracing_subscriber::fmt()
            .with_env_filter(
                tracing_subscriber::EnvFilter::builder()
                    .with_default_directive(tracing::level_filters::LevelFilter::INFO.into())
                    .from_env_lossy(),
            )
            .init();
    });

    let mxl_api = mxl::load_api(get_mxl_so_path()).unwrap();
    let domain_guard = TestDomainGuard::new(test);
    (
        MxlInstance::new(mxl_api, domain_guard.domain().as_str(), "").unwrap(),
        domain_guard,
    )
}

fn read_flow_def<P: AsRef<std::path::Path>>(path: P) -> String {
    let flow_config_file = mxl::config::get_mxl_repo_root().join(path);

    std::fs::read_to_string(flow_config_file.as_path())
        .map_err(|error| {
            mxl::Error::Other(format!(
                "Error while reading flow definition from \"{}\": {}",
                flow_config_file.display(),
                error
            ))
        })
        .unwrap()
}

fn prepare_flow_config_info<P: AsRef<std::path::Path>>(
    mxl_instance: &MxlInstance,
    path: P,
) -> mxl::FlowConfigInfo {
    let flow_def = read_flow_def(path);
    mxl_instance.create_flow(flow_def.as_str(), None).unwrap()
}

#[test]
fn basic_mxl_grain_writing_reading() {
    let (mxl_instance, _domain_guard) = setup_test("grains");
    let flow_config_info = prepare_flow_config_info(&mxl_instance, "lib/tests/data/v210_flow.json");
    let flow_id = flow_config_info.common().id().to_string();
    let flow_writer = mxl_instance.create_flow_writer(flow_id.as_str()).unwrap();
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
    mxl_instance.destroy_flow(flow_id.as_str()).unwrap();
    mxl_instance.destroy().unwrap();
}

#[test]
fn basic_mxl_samples_writing_reading() {
    let (mxl_instance, _domain_guard) = setup_test("samples");
    let flow_info = prepare_flow_config_info(&mxl_instance, "lib/tests/data/audio_flow.json");
    let flow_id = flow_info.common().id().to_string();
    let flow_writer = mxl_instance.create_flow_writer(flow_id.as_str()).unwrap();
    let samples_writer = flow_writer.to_samples_writer().unwrap();
    let flow_reader = mxl_instance.create_flow_reader(flow_id.as_str()).unwrap();
    let samples_reader = flow_reader.to_samples_reader().unwrap();
    let rate = flow_info.common().sample_rate().unwrap();
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
    mxl_instance.destroy_flow(flow_id.as_str()).unwrap();
    mxl_instance.destroy().unwrap();
}

#[test]
fn get_flow_def() {
    let (mxl_instance, _domain_guard) = setup_test("flow_def");
    let flow_def = read_flow_def("lib/tests/data/v210_flow.json");
    let flow_info = mxl_instance.create_flow(flow_def.as_str(), None).unwrap();
    let flow_id = flow_info.common().id().to_string();
    let retrieved_flow_def = mxl_instance.get_flow_def(flow_id.as_str()).unwrap();
    assert_eq!(flow_def, retrieved_flow_def);
    mxl_instance.destroy_flow(flow_id.as_str()).unwrap();
    mxl_instance.destroy().unwrap();
}
