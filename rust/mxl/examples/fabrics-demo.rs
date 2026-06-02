// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

mod common;

use std::{
    sync::{
        Arc,
        atomic::{self, AtomicBool},
    },
    time::Duration,
};

use clap::{Parser, Subcommand};

use base64::{Engine as _, prelude::BASE64_STANDARD};

use mxl::{
    Error, FlowConfigInfo, FlowInfo, FlowReader, FlowWriter, GrainReader, GrainWriter,
    MxlFabricsApi, MxlInstance, SamplesReader, SamplesWriter,
    config::{get_mxl_fabrics_ofi_so_path, get_mxl_so_path},
    fabrics::{
        EndpointAddress, FabricsInstance, TargetInfo,
        initiator::{self, Initiator},
        target::{self, Target},
    },
};

#[derive(Debug, Parser)]
#[command(
    version = clap::crate_version!(),
    author = clap::crate_authors!(),
    subcommand_required = true,
    arg_required_else_help = true
)]
pub struct Cli {
    #[arg(short, long, help = "The MXL domain directory")]
    pub domain: String,

    #[arg(
        short,
        long,
        default_value = "tcp",
        help = "The fabrics provider. One of (tcp, verbs or efa)."
    )]
    pub provider: String,

    #[arg(
        short,
        long,
        help = "This corresponds to the interface identifier of the fabrics endpoint, it can also be a logical address. This can be seen as the bind address when using sockets."
    )]
    pub node: String,

    #[arg(
        short,
        long,
        help = "This corresponds to a service identifier for the fabrics endpoint. This can be seen as the bind port when using sockets."
    )]
    pub service: String,

    #[command(subcommand)]
    pub command: Command,
}

#[derive(Debug, Subcommand)]
pub enum Command {
    /// Run as a receiver (fabrics target + flow writer).
    Target {
        #[arg(
            long,
            help = "The JSON file which contains the NMOS Flow configuration."
        )]
        flow_file: String,
    },
    /// Run as an initiator (flow reader + fabrics initiator).
    Initiator {
        #[arg(long, help = "The flow ID to read from.")]
        flow_id: String,
        #[arg(
            long,
            help = "The target information to send to. Start the target first and paste the printed target info here."
        )]
        target_info: String,
    },
}

struct TargetEndpoint<'a> {
    _instance: &'a MxlInstance,
    flow_config: FlowConfigInfo,
    flow_writer: FlowWriter,
    target: Target<target::states::Specializing>,
}

impl<'a> TargetEndpoint<'a> {
    pub fn new(
        instance: &'a MxlInstance,
        fabrics_api: &Arc<MxlFabricsApi>,
        flow_file: &str,
        cli: &Cli,
    ) -> Result<(Self, TargetInfo), mxl::Error> {
        let flow_config_str = std::fs::read_to_string(flow_file).expect("Failed to read flow file");

        let (flow_writer, flow_config, _) = instance.create_flow_writer(&flow_config_str, None)?;

        let fabrics_instance = instance.create_fabrics_instance(fabrics_api)?;

        let provider = fabrics_instance.provider_from_str(&cli.provider)?;

        let target_config = target::Config::new(
            EndpointAddress {
                node: Some(&cli.node),
                service: Some(&cli.service),
            },
            provider,
            &flow_writer,
        );

        let target = fabrics_instance.create_target()?;
        let (target, target_info) = target.setup(&target_config)?;

        Ok((
            Self {
                _instance: instance,
                flow_config,
                flow_writer,
                target,
            },
            target_info,
        ))
    }

    pub fn run(self, running: Arc<AtomicBool>) -> Result<(), mxl::Error> {
        match self.target.specialize(&self.flow_config) {
            target::Either::Grain(target) => {
                Self::run_discrete(target, self.flow_writer.to_grain_writer()?, running)?;
            }
            target::Either::Sample(target) => {
                Self::run_continuous(target, self.flow_writer.to_samples_writer()?, running)?;
            }
        }
        Ok(())
    }

    fn run_discrete(
        target: Target<target::states::Grain>,
        writer: GrainWriter,
        running: Arc<AtomicBool>,
    ) -> Result<(), mxl::Error> {
        while running.load(atomic::Ordering::SeqCst) {
            match target.read(Duration::from_millis(200)) {
                Ok(read_result) => {
                    let grain = writer.open_grain(read_result.grain_index)?;
                    let valid_slices = grain.valid_slices();
                    grain.commit(valid_slices)?;

                    tracing::debug!(
                        "Commited grain index {}, slice index {}.",
                        read_result.grain_index,
                        valid_slices
                    );
                }
                Err(mxl::Error::NotReady) => {
                    continue;
                }
                Err(mxl::Error::Interrupted) => {
                    tracing::info!("Interrupted, exiting.");
                    break;
                }
                Err(e) => {
                    return Err(e);
                }
            }
        }
        Ok(())
    }

    fn run_continuous(
        target: Target<target::states::Sample>,
        writer: SamplesWriter,
        running: Arc<AtomicBool>,
    ) -> Result<(), mxl::Error> {
        while running.load(atomic::Ordering::SeqCst) {
            match target.read(Duration::from_millis(200)) {
                Ok(read_result) => {
                    let samples = writer.open_samples(read_result.head_index, read_result.count)?;
                    samples.commit()?;

                    tracing::debug!(
                        "Commited samples, head index {}, count {}.",
                        read_result.head_index,
                        read_result.count
                    );
                }
                Err(mxl::Error::NotReady) => {
                    continue;
                }
                Err(mxl::Error::Interrupted) => {
                    tracing::info!("Interrupted, exiting.");
                    break;
                }
                Err(e) => {
                    return Err(e);
                }
            }
        }
        Ok(())
    }
}

struct InitiatorEndpoint<'a> {
    instance: &'a MxlInstance,
    fabrics_instance: FabricsInstance,
    flow_reader: FlowReader,
    initiator: Initiator<initiator::states::Specializing>,
}

impl<'a> InitiatorEndpoint<'a> {
    pub fn new(
        instance: &'a MxlInstance,
        fabrics_api: &Arc<MxlFabricsApi>,
        flow_id: &str,
        cli: &Cli,
    ) -> Result<Self, mxl::Error> {
        let flow_reader = instance.create_flow_reader(flow_id)?;

        let fabrics_instance = instance.create_fabrics_instance(fabrics_api)?;

        let initiator = fabrics_instance.create_initiator()?;

        let provider = fabrics_instance.provider_from_str(&cli.provider)?;

        let initiator_config = initiator::Config::new(
            EndpointAddress {
                node: Some(&cli.node),
                service: Some(&cli.service),
            },
            provider,
            &flow_reader,
        );

        let initiator = initiator.setup(&initiator_config)?;

        Ok(Self {
            instance,
            fabrics_instance,
            initiator,
            flow_reader,
        })
    }

    pub fn run(self, target_info_str: &str, running: Arc<AtomicBool>) -> Result<(), mxl::Error> {
        let flow_info = self.flow_reader.get_info()?;

        let target_info_str =
            String::from_utf8(BASE64_STANDARD.decode(target_info_str).map_err(|e| {
                Error::Other(format!("Failed to decode target_info from base64: {e}"))
            })?)
            .map_err(|e| Error::Other(format!("Decoded target_info is not valid UTF-8: {e}")))?;

        let target_info = self
            .fabrics_instance
            .target_info_from_str(&target_info_str)?;

        match self.initiator.specialize(&flow_info.config) {
            initiator::Either::Grain(initiator) => {
                initiator.add_target(&target_info)?;
                // Wait to be connected
                loop {
                    if !running.load(atomic::Ordering::SeqCst) {
                        return Ok(());
                    }

                    if initiator.make_progress(Duration::from_millis(250)).is_ok() {
                        break;
                    }
                }
                Self::run_discrete(
                    self.instance,
                    initiator,
                    self.flow_reader.to_grain_reader()?,
                    &flow_info,
                    running,
                )?;
            }
            initiator::Either::Samples(initiator) => {
                initiator.add_target(&target_info)?;
                // Wait to be connected
                loop {
                    if !running.load(atomic::Ordering::SeqCst) {
                        return Ok(());
                    }

                    if initiator.make_progress(Duration::from_millis(250)).is_ok() {
                        break;
                    }
                }
                Self::run_continuous(
                    self.instance,
                    initiator,
                    self.flow_reader.to_samples_reader()?,
                    &flow_info,
                    running,
                )?;
            }
        }

        tracing::info!("Stopping as requested.");

        Ok(())
    }

    fn run_discrete(
        instance: &MxlInstance,
        initiator: Initiator<initiator::states::Grain>,
        reader: GrainReader,
        flow_info: &FlowInfo,
        running: Arc<AtomicBool>,
    ) -> Result<(), mxl::Error> {
        let rate = flow_info.config.common().grain_rate()?;
        let mut index = instance.get_current_index(&rate);
        while running.load(atomic::Ordering::SeqCst) {
            match reader.get_complete_grain(index, Duration::from_millis(200)) {
                Ok(grain) => {
                    match initiator.transfer(index, 0, grain.total_slices) {
                        Err(Error::NotReady) => {
                            // Retry the same grain
                            continue;
                        }
                        Err(e) => {
                            return Err(e);
                        }
                        Ok(_) => {}
                    };

                    // Transfer was posted, now wait for completion
                    loop {
                        match initiator.make_progress(Duration::from_millis(10)) {
                            Ok(_) => {
                                // we're done exiting the loop
                                break;
                            }
                            Err(Error::Interrupted) => {
                                return Ok(());
                            }
                            Err(Error::NotReady) => {
                                // Retry
                                continue;
                            }
                            Err(e) => {
                                return Err(e);
                            }
                        }
                    }
                    index += 1;
                }
                Err(Error::OutOfRangeTooLate) => {
                    // We are too late, move to the next grain
                    index = instance.get_current_index(&rate);
                }
                Err(Error::OutOfRangeTooEarly) => {
                    // We are too early, retry the same grain
                }
                Err(e) => {
                    tracing::error!("Error reading from flow: {}.", e);
                }
            }
        }
        Ok(())
    }

    fn run_continuous(
        instance: &MxlInstance,
        initiator: Initiator<initiator::states::Samples>,
        reader: SamplesReader,
        flow_info: &FlowInfo,
        running: Arc<AtomicBool>,
    ) -> Result<(), mxl::Error> {
        let rate = flow_info.config.common().grain_rate()?;
        let count = flow_info.config.common().max_sync_batch_size_hint() as usize;
        let mut index = instance.get_current_index(&rate);
        while running.load(atomic::Ordering::SeqCst) {
            match reader.get_samples_non_blocking(index, count) {
                Ok(_sample) => {
                    match initiator.transfer(index, count) {
                        Err(Error::NotReady) => {
                            // Retry the same grain
                            continue;
                        }
                        Err(e) => {
                            return Err(e);
                        }
                        Ok(_) => {}
                    };

                    // Transfer was posted, now wait for completion
                    loop {
                        match initiator.make_progress(Duration::from_millis(10)) {
                            Ok(_) => {
                                // we're done exiting the loop
                                break;
                            }
                            Err(Error::Interrupted) => {
                                return Ok(());
                            }
                            Err(Error::NotReady) => {
                                // Retry
                                continue;
                            }
                            Err(e) => {
                                return Err(e);
                            }
                        }
                    }
                    index += 1;
                }
                Err(Error::OutOfRangeTooLate) => {
                    // We are too late, move to the next grain
                    index = instance.get_current_index(&rate);
                }
                Err(Error::OutOfRangeTooEarly) => {
                    // We are too early, retry the same grain
                }
                Err(e) => {
                    tracing::error!("Error reading from flow: {}.", e);
                }
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), mxl::Error> {
    common::setup_logging();

    let cli = Cli::parse();

    let running = Arc::new(AtomicBool::new(true));
    let running2 = running.clone();
    ctrlc::set_handler(move || {
        running2.store(false, atomic::Ordering::SeqCst);
    })
    .expect("Error setting Ctrl-C handler");

    let api = mxl::load_api(get_mxl_so_path())?;

    let fabrics_api = mxl::load_fabrics_api(get_mxl_fabrics_ofi_so_path())?;

    let instance = mxl::MxlInstance::new(api, &cli.domain, "")?;

    match &cli.command {
        Command::Initiator {
            flow_id,
            target_info,
        } => {
            let initiator = InitiatorEndpoint::new(&instance, &fabrics_api, flow_id, &cli)?;
            initiator.run(target_info, running)?;
        }
        Command::Target { flow_file } => {
            let (target, target_info) =
                TargetEndpoint::new(&instance, &fabrics_api, flow_file, &cli)?;
            tracing::info!(
                "Target Info: {}",
                BASE64_STANDARD.encode(target_info.to_string()?)
            );
            target.run(running)?;
        }
    }

    Ok(())
}
