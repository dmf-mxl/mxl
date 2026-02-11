// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Basic integration tests for the MXL Rust bindings.
//!
//! These tests exercise the core read/write operations for both discrete (grain-based)
//! and continuous (sample-based) flows. Each test creates an isolated temporary domain
//! on `/dev/shm` and cleans up automatically.
//!
//! # Test Coverage
//!
//! - Grain writing and reading (video/data flows)
//! - Sample writing and reading (audio flows)
//! - Flow definition retrieval
//! - Instance and flow lifecycle management
//!
//! # Requirements
//!
//! - MXL library must be built and available (via `get_mxl_so_path()`)
//! - `/dev/shm` must be writable
//! - Test flow definitions must exist in the MXL repository

use std::time::Duration;

use mxl::{MxlInstance, OwnedGrainData, OwnedSamplesData, config::get_mxl_so_path};
use tracing::info;

/// Ensures logging is initialized only once across all tests.
static LOG_ONCE: std::sync::Once = std::sync::Once::new();

/// RAII guard for test domain directories.
///
/// Automatically creates a unique temporary domain directory on `/dev/shm`
/// and removes it when dropped, ensuring test isolation and cleanup.
struct TestDomainGuard {
    dir: std::path::PathBuf,
}

impl TestDomainGuard {
    /// Creates a new test domain directory with a unique UUID suffix.
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

    /// Returns the domain path as a string.
    fn domain(&self) -> String {
        self.dir.to_string_lossy().to_string()
    }
}

impl Drop for TestDomainGuard {
    /// Removes the test domain directory on drop.
    fn drop(&mut self) {
        std::fs::remove_dir_all(self.dir.as_path()).unwrap_or_else(|_| {
            panic!(
                "Failed to remove test domain directory \"{}\".",
                self.dir.display()
            )
        });
    }
}

/// Sets up a test by initializing logging and creating an isolated MXL instance.
///
/// Returns an MXL instance bound to a unique temporary domain, along with the
/// domain guard for cleanup.
fn setup_test(test: &str) -> (MxlInstance, TestDomainGuard) {
    // Initialize logging once (respects RUST_LOG environment variable)
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

/// Reads a flow definition JSON file from the MXL repository test data directory.
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

/// Tests basic grain writing and reading for discrete flows.
///
/// Creates a video flow (v210), writes a grain, reads it back, and verifies
/// the roundtrip. Demonstrates zero-copy access and RAII cleanup.
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

/// Tests basic sample writing and reading for continuous flows.
///
/// Creates an audio flow, writes a batch of samples, reads them back, and verifies
/// the roundtrip. Demonstrates multi-channel access and RAII cleanup.
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

/// Tests flow definition retrieval.
///
/// Creates a flow from a JSON definition, retrieves it back from the domain,
/// and verifies the JSON matches the original.
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
