use std::time::Duration;

use clap::Parser;
use mxl::config::get_mxf_so_path;
use tracing::info;

const READ_TIMEOUT: Duration = Duration::from_secs(5);

#[derive(Debug, Parser)]
#[command(version = clap::crate_version!(), author = clap::crate_authors!())]
pub struct Opts {
    /// The path to the shmem directory where the mxl domain is mapped.
    #[arg(long)]
    pub mxl_domain: String,

    /// The id of the flow.
    #[arg(long)]
    pub flow_id: String,
}

fn main() -> Result<(), mxl::Error> {
    setup_logging();
    let opts: Opts = Opts::parse();

    let mxl_api = mxl::load_api(get_mxf_so_path())?;
    let mxl_instance = mxl::MxlInstance::new(mxl_api, &opts.mxl_domain, "")?;
    let reader = mxl_instance.create_flow_reader(&opts.flow_id)?;
    let flow_info = reader.get_info()?;
    let rate = flow_info.discrete_flow_info()?.grainRate;
    let current_head_index = mxl_instance.get_current_head_index(&rate);

    info!("Grain rate: {}/{}", rate.numerator, rate.denominator);

    for index in current_head_index.. {
        let grain_data = reader.get_complete_grain(index, READ_TIMEOUT)?;
        info!(
            "Index: {index} Grain data len: {:?}",
            grain_data.payload.len()
        );
    }

    Ok(())
}

fn setup_logging() {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::builder()
                .with_default_directive(tracing::level_filters::LevelFilter::INFO.into())
                .from_env_lossy(),
        )
        .init();
}
