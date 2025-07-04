/// Tests of the basic low level synchronous API.
///
/// The tests now require an MXL library of a specific name to be present in the system. This should
/// change in the future. For now, feel free to just edit the path to your library.
use std::time::Duration;

use mxl::{OwnedGrainData, config::get_mxf_so_path};
use tracing::info;

static LOG_ONCE: std::sync::Once = std::sync::Once::new();

fn setup_test() -> mxl::MxlInstance {
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

    let mxl_api = mxl::load_api(get_mxf_so_path()).unwrap();
    // TODO: Randomize the domain name to allow parallel tests run.
    mxl::MxlInstance::new(mxl_api, "/dev/shm/mxl_domain", "").unwrap()
}

#[test]
fn basic_mxl_writing_reading() {
    // TODO: Add the writing part of the test. Now the test requires the external MXL tool to write
    //       data: `tools/mxl-gst/mxl-gst-videotestsrc -d /dev/shm/mxl_domain -f
    //       ../../lib/tests/data/v210_flow.json`
    let mxl_instance = setup_test();
    let flow_reader = mxl_instance
        .create_flow_reader("5fbec3b1-1b0f-417d-9059-8b94a47197ed")
        .unwrap();
    let flow_info = flow_reader.get_info().unwrap();
    let rate = flow_info.discrete_flow_info().unwrap().grainRate;
    let current_head_index = mxl_instance.get_current_head_index(&rate);
    let grain_data = flow_reader
        .get_complete_grain(current_head_index, Duration::from_secs(5))
        .unwrap();
    let grain_data: OwnedGrainData = grain_data.into();
    info!("Grain data len: {:?}", grain_data.payload.len());
    flow_reader.destroy().unwrap();
    unsafe { mxl_instance.destroy() }.unwrap();
}
