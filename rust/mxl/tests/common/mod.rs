// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use mxl::{MxlInstance, config::get_mxl_so_path};

#[cfg(feature = "mxl-fabrics-ofi")]
use mxl::{MxlFabricsApi, config::get_mxl_fabrics_ofi_so_path};

static LOG_ONCE: std::sync::Once = std::sync::Once::new();

pub struct TestDomainGuard {
    dir: std::path::PathBuf,
}

impl TestDomainGuard {
    fn new(test: &str) -> Self {
        let dir = std::path::PathBuf::from(format!(
            "/dev/shm/mxl_rust_tests_domain_{}_{}",
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

pub fn setup_test(test: &str) -> (MxlInstance, TestDomainGuard) {
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

#[cfg(feature = "mxl-fabrics-ofi")]
#[allow(dead_code)]
pub fn load_fabrics_test_api() -> std::sync::Arc<MxlFabricsApi> {
    mxl::load_fabrics_api(get_mxl_fabrics_ofi_so_path()).unwrap()
}

pub fn read_flow_def<P: AsRef<std::path::Path>>(path: P) -> String {
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
