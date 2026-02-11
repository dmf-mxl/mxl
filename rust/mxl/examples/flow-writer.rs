// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Example program demonstrating MXL flow writing.
//!
//! This example creates an MXL flow from a JSON definition file and continuously
//! writes media data (either discrete grains for video/data or continuous samples
//! for audio). It demonstrates timing synchronization, zero-copy writing, and
//! automatic flow cleanup.
//!
//! # Usage
//!
//! For video/data flows:
//!
//! ```bash
//! cargo run --example flow-writer -- \
//!     --mxl-domain /dev/shm/my_domain \
//!     --flow-config-file examples/v210_flow.json \
//!     --grain-or-sample-count 100
//! ```
//!
//! For audio flows with custom batch size:
//!
//! ```bash
//! cargo run --example flow-writer -- \
//!     --mxl-domain /dev/shm/my_domain \
//!     --flow-config-file examples/audio_flow.json \
//!     --sample-batch-size 480
//! ```

mod common;

use clap::Parser;
use tracing::{info, warn};

use mxl::config::get_mxl_so_path;

/// Command-line arguments for the flow writer example.
#[derive(Debug, Parser)]
#[command(version = clap::crate_version!(), author = clap::crate_authors!())]
pub struct Opts {
    /// The path to the shmem directory where the mxl domain is mapped.
    #[arg(long)]
    pub mxl_domain: String,

    /// The path to the configuration file describing the flow we want to write to.
    #[arg(long)]
    pub flow_config_file: String,

    /// The number of grains to write. If not specified, will run until stopped.
    #[arg(long)]
    pub grain_or_sample_count: Option<u64>,

    /// The number of samples to be written in one open samples call. Is only valid for "continuous"
    /// flows. If not specified, will more or less fit 10 ms.
    #[arg(long)]
    pub sample_batch_size: Option<u64>,
}

/// Main entry point - loads MXL, creates flow, and dispatches to appropriate writer.
fn main() -> Result<(), mxl::Error> {
    common::setup_logging();
    let opts: Opts = Opts::parse();

    let mxl_api = mxl::load_api(get_mxl_so_path())?;
    let mxl_instance = mxl::MxlInstance::new(mxl_api, &opts.mxl_domain, "")?;
    let flow_def = std::fs::read_to_string(opts.flow_config_file.as_str()).map_err(|error| {
        mxl::Error::Other(format!(
            "Error while reading flow definition from \"{}\": {}",
            &opts.flow_config_file, error
        ))
    })?;

    let (writer, flow_config_info, was_created) =
        mxl_instance.create_flow_writer(flow_def.as_str(), None)?;
    if !was_created {
        warn!("Reusing existing flow.");
    }

    if flow_config_info.common().is_discrete_flow() {
        if opts.sample_batch_size.is_some() {
            return Err(mxl::Error::Other(
                "Sample batch size is only relevant for \"continuous\" flows.".to_owned(),
            ));
        }
        write_grains(
            mxl_instance,
            flow_config_info,
            writer.to_grain_writer()?,
            opts.grain_or_sample_count,
        )
    } else {
        write_samples(
            mxl_instance,
            flow_config_info,
            writer.to_samples_writer()?,
            opts.grain_or_sample_count,
            opts.sample_batch_size,
        )
    }
}

/// Writes discrete grains (video frames / data packets) with timing synchronization.
///
/// Demonstrates zero-copy grain writing with a test pattern, automatic pacing
/// based on grain rate, and explicit flow cleanup.
pub fn write_grains(
    mxl_instance: mxl::MxlInstance,
    flow_config_info: mxl::FlowConfigInfo,
    writer: mxl::GrainWriter,
    grain_count: Option<u64>,
) -> Result<(), mxl::Error> {
    let flow_id = flow_config_info.common().id().to_string();
    let grain_rate = flow_config_info.common().grain_rate()?;
    let mut grain_index = mxl_instance.get_current_index(&grain_rate);
    info!(
        "Will write to flow \"{flow_id}\" with grain rate {}/{} starting from index {grain_index}.",
        grain_rate.numerator, grain_rate.denominator
    );

    let mut remaining_grains = grain_count;
    loop {
        if let Some(count) = remaining_grains {
            if count == 0 {
                break;
            }
            remaining_grains = Some(count - 1);
        }

        let mut grain_writer_access = writer.open_grain(grain_index)?;
        let total_slices = grain_writer_access.total_slices();
        let payload = grain_writer_access.payload_mut();
        let payload_len = payload.len();
        for (i, byte) in payload.iter_mut().enumerate() {
            *byte = ((i as u64 + grain_index) % 256) as u8;
        }
        grain_writer_access.commit(total_slices)?;

        let timestamp = mxl_instance.index_to_timestamp(grain_index + 1, &grain_rate)?;
        let sleep_duration = mxl_instance.get_duration_until_index(grain_index + 1, &grain_rate)?;
        info!(
            "Finished writing {payload_len} bytes ({total_slices} slices) into grain {grain_index}, will sleep \
             for {:?} until timestamp {timestamp}.",
            sleep_duration
        );
        grain_index += 1;
        mxl_instance.sleep_for(sleep_duration);
    }

    info!("Finished writing requested number of grains, deleting the flow.");
    writer.destroy()?;
    Ok(())
}

/// Writes continuous audio samples in batches with timing synchronization.
///
/// Demonstrates zero-copy multi-channel sample writing with a test pattern,
/// automatic pacing based on sample rate, and explicit flow cleanup.
pub fn write_samples(
    mxl_instance: mxl::MxlInstance,
    flow_config_info: mxl::FlowConfigInfo,
    writer: mxl::SamplesWriter,
    sample_count: Option<u64>,
    batch_size: Option<u64>,
) -> Result<(), mxl::Error> {
    let flow_id = flow_config_info.common().id().to_string();
    let sample_rate = flow_config_info.common().sample_rate()?;
    let batch_size =
        batch_size.unwrap_or((sample_rate.numerator / (100 * sample_rate.denominator)) as u64);
    let mut samples_index = mxl_instance.get_current_index(&sample_rate);
    info!(
        "Will write to flow \"{flow_id}\" with sample rate {}/{}, using batches of size {batch_size} samples, first batch ending at index {samples_index}.",
        sample_rate.numerator, sample_rate.denominator
    );

    let mut remaining_samples = sample_count;
    loop {
        if let Some(count) = remaining_samples
            && count == 0
        {
            break;
        }
        let samples_to_write = u64::min(batch_size, remaining_samples.unwrap_or(u64::MAX));
        if let Some(count) = remaining_samples {
            remaining_samples = Some(count.saturating_sub(batch_size));
        }

        let mut samples_write_access = writer.open_samples(samples_index, batch_size as usize)?;
        let mut writing_sample_index = samples_index - batch_size + 1;
        for channel in 0..samples_write_access.channels() {
            let (data_1, data_2) = samples_write_access.channel_data_mut(channel)?;
            for sample in data_1.iter_mut() {
                *sample = (writing_sample_index % 256) as u8;
                writing_sample_index += 1;
            }
            for sample in data_2.iter_mut() {
                *sample = (writing_sample_index % 256) as u8;
                writing_sample_index += 1;
            }
        }
        samples_write_access.commit()?;

        let timestamp =
            mxl_instance.index_to_timestamp(samples_index + batch_size, &sample_rate)?;
        let sleep_duration =
            mxl_instance.get_duration_until_index(samples_index + batch_size, &sample_rate)?;
        info!(
            "Finished writing {samples_to_write} samples into batch ending with index {samples_index}, will sleep for {:?} until timestamp {timestamp}.",
            sleep_duration
        );
        samples_index += batch_size;
        mxl_instance.sleep_for(sleep_duration);
    }

    info!("Finished writing requested number of samples, deleting the flow.");
    writer.destroy()?;
    Ok(())
}
