// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

mod common;

use std::time::Duration;

use clap::Parser;
use mxl::config::get_mxl_so_path;
use tracing::{info, warn};

const READ_TIMEOUT: Duration = Duration::from_secs(5);

#[derive(Debug, Parser)]
#[command(version = clap::crate_version!(), author = clap::crate_authors!())]
pub struct Opts {
    /// The path to the shmem directory where the mxl domain is mapped.
    #[arg(long)]
    pub mxl_domain: String,

    /// The id of the flow to read.
    #[arg(long)]
    pub flow_id: String,

    /// The number of samples to be read in one open samples call. Is only valid for "continuous"
    /// flows. If not specified, the value provided by the writer will be used if available, or this
    /// will more or less fit 10 ms as a fallback.
    #[arg(long)]
    pub sample_batch_size: Option<u64>,
}

fn main() -> Result<(), mxl::Error> {
    common::setup_logging();
    let opts: Opts = Opts::parse();

    let mxl_api = mxl::load_api(get_mxl_so_path())?;
    let mxl_instance = mxl::MxlInstance::new(mxl_api, &opts.mxl_domain, "")?;
    let reader = mxl_instance.create_flow_reader(&opts.flow_id)?;
    let flow_info = reader.get_info()?;
    if flow_info.is_discrete_flow() {
        if opts.sample_batch_size.is_some() {
            return Err(mxl::Error::Other(
                "Sample batch size is only relevant for \"continuous\" flows.".to_owned(),
            ));
        }
        read_grains(mxl_instance, reader.to_grain_reader()?, flow_info)
    } else {
        read_samples(
            mxl_instance,
            reader.to_samples_reader()?,
            flow_info,
            opts.sample_batch_size,
        )
    }
}

fn read_grains(
    mxl_instance: mxl::MxlInstance,
    reader: mxl::GrainReader,
    flow_info: mxl::FlowInfo,
) -> Result<(), mxl::Error> {
    let rate = flow_info.discrete_flow_info()?.grainRate;
    let current_index = mxl_instance.get_current_index(&rate);

    info!("Grain rate: {}/{}", rate.numerator, rate.denominator);

    for index in current_index.. {
        let grain_data = reader.get_complete_grain(index, READ_TIMEOUT)?;
        info!(
            "Index: {index} Grain data len: {:?}",
            grain_data.payload.len()
        );
    }

    Ok(())
}

fn read_samples(
    mxl_instance: mxl::MxlInstance,
    reader: mxl::SamplesReader,
    flow_info: mxl::FlowInfo,
    batch_size: Option<u64>,
) -> Result<(), mxl::Error> {
    let flow_id = flow_info.common_flow_info().id().to_string();
    let sample_rate = flow_info.continuous_flow_info()?.sampleRate;
    let common_flow_info = flow_info.common_flow_info();
    let batch_size = if let Some(batch_size) = batch_size {
        if common_flow_info.max_commit_batch_size_hint() != 0
            && batch_size != common_flow_info.max_commit_batch_size_hint() as u64
        {
            warn!(
                "Writer batch size is set to {}, but sample batch size is provided, using the \
                 latter.",
                common_flow_info.max_commit_batch_size_hint()
            );
        }
        batch_size as usize
    } else if common_flow_info.max_commit_batch_size_hint() == 0 {
        let batch_size = (sample_rate.numerator / (100 * sample_rate.denominator)) as usize;
        warn!(
            "Writer batch size not available, using fallback value of {}.",
            batch_size
        );
        batch_size
    } else {
        common_flow_info.max_commit_batch_size_hint() as usize
    };
    let mut read_head = reader.get_info()?.continuous_flow_info()?.headIndex;
    let mut read_head_valid_at = mxl_instance.get_time();
    info!(
        "Will read from flow \"{flow_id}\" with sample rate {}/{}, using batches of size \
        {batch_size} samples, first batch ending at index {read_head}.",
        sample_rate.numerator, sample_rate.denominator
    );
    loop {
        let samples_data = reader.get_samples(read_head, batch_size)?;
        info!(
            "Read samples for {} channel(s) at index {}.",
            samples_data.num_of_channels(),
            read_head
        );
        if samples_data.num_of_channels() > 0 {
            let channel_data = samples_data.channel_data(0)?;
            info!(
                "Buffer size for channel 0 is ({}, {}).",
                channel_data.0.len(),
                channel_data.1.len()
            );
        }
        // MXL currently does not have any samples reading mechanism which would wait for data to be
        // available.
        // We will just blindly assume that more data will be available when we need them.
        // This, of course, does not have to be true, because the write batch size may be larger
        // than our reading batch size.
        let next_head = read_head + batch_size as u64;
        let next_head_timestamp = mxl_instance.index_to_timestamp(next_head, &sample_rate)?;
        let read_head_timestamp = mxl_instance.index_to_timestamp(read_head, &sample_rate)?;
        let read_batch_duration = next_head_timestamp - read_head_timestamp;
        let deadline = std::time::Instant::now() + READ_TIMEOUT;
        loop {
            read_head_valid_at += read_batch_duration;
            let sleep_duration =
                Duration::from_nanos(read_head_valid_at.saturating_sub(mxl_instance.get_time()));
            info!("Will sleep for {:?}.", sleep_duration);
            mxl_instance.sleep_for(sleep_duration);
            if std::time::Instant::now() >= deadline {
                warn!("Timeout while waiting for samples at index {}.", next_head);
                return Err(mxl::Error::Timeout);
            }
            let available_head = reader.get_info()?.continuous_flow_info()?.headIndex;
            if available_head >= next_head {
                break;
            }
        }
        read_head = next_head;
    }
}
