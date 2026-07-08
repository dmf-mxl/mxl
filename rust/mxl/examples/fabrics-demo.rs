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
    Error, FlowConfigInfo, FlowInfo, FlowReader, FlowWriter, GrainReader, GrainWriter, MxlInstance,
    SamplesReader, SamplesWriter,
    config::{get_mxl_fabrics_ofi_so_path, get_mxl_so_path},
    fabrics::{
        Capabilities, EndpointAddress, FabricsInstance, InterfaceConfig, ProviderType, TargetInfo,
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
        help = "Force a specific provider (tcp, verbs, efa or shm). Auto-selected if not specified."
    )]
    pub provider: Option<String>,

    #[arg(
        short,
        long,
        help = "Filter interface selection by node address. If not set, the best available interface is chosen automatically."
    )]
    pub node: Option<String>,

    #[arg(
        short,
        long,
        help = "Service identifier for the fabrics endpoint (e.g. a port number)."
    )]
    pub service: Option<String>,

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
        #[arg(
            long,
            help = "Output file path for raw target info (optional, always logged as base64)."
        )]
        target_info_path: Option<String>,
        //TODO: flow options?
    },
    /// Run as an initiator (flow reader + fabrics initiator).
    Initiator {
        #[arg(long, help = "The flow ID to read from.")]
        flow_id: String,
        #[arg(
            long,
            help = "Base64-encoded target info, or a path prefixed with '@' to read raw target info from a file."
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
        fabrics_instance: &FabricsInstance,
        interface: InterfaceConfig,
        flow_file: &str,
    ) -> Result<(Self, TargetInfo), mxl::Error> {
        let flow_config_str = std::fs::read_to_string(flow_file).expect("Failed to read flow file");

        let (flow_writer, flow_config, _) = instance.create_flow_writer(&flow_config_str, None)?;

        let target_config = target::Config::new(interface, &flow_writer);

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
        fabrics_instance: FabricsInstance,
        interface: InterfaceConfig,
        flow_id: &str,
    ) -> Result<Self, mxl::Error> {
        let flow_reader = instance.create_flow_reader(flow_id)?;

        let initiator = fabrics_instance.create_initiator()?;

        let initiator_config = initiator::Config::new(interface, &flow_reader);

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

#[derive(Debug, Clone, PartialEq, Eq)]
struct ProviderPrio(ProviderType);
impl ProviderPrio {
    fn priority(&self) -> u32 {
        match self {
            ProviderPrio(ProviderType::Efa) => 4,
            ProviderPrio(ProviderType::Verbs) => 3,
            ProviderPrio(ProviderType::Tcp) => 2,
            ProviderPrio(ProviderType::Shm) => 1,
            _ => 0,
        }
    }
}
impl Ord for ProviderPrio {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.priority().cmp(&other.priority())
    }
}
impl PartialOrd for ProviderPrio {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

fn main() -> Result<(), anyhow::Error> {
    common::setup_logging();

    let cli = Cli::parse();

    tracing::info!(domain = %cli.domain, provider = ?cli.provider, node = ?cli.node, service = ?cli.service, command = ?cli.command, "Starting fabrics demo");

    let running = Arc::new(AtomicBool::new(true));
    let running2 = running.clone();
    ctrlc::set_handler(move || {
        running2.store(false, atomic::Ordering::SeqCst);
    })
    .expect("Error setting Ctrl-C handler");

    let api = mxl::load_api(get_mxl_so_path())?;

    let fabrics_api = mxl::load_fabrics_api(get_mxl_fabrics_ofi_so_path())?;

    let instance = mxl::MxlInstance::new(api, &cli.domain, "")?;

    let fabrics_instance = instance.create_fabrics_instance(&fabrics_api)?;

    let endpoint_address = EndpointAddress {
        node: cli.node.as_deref(),
        service: cli.service.as_deref(),
    };

    let provider =
        fabrics_instance.provider_from_str(&cli.provider.unwrap_or("any".to_string()))?;

    let interface_config = mxl::fabrics::InterfaceConfig::builder(endpoint_address)
        .provider(provider.prov_type().clone())
        .caps(Capabilities::builder().supports_remote_write().build())
        .build()?;

    let interfaces = fabrics_instance
        .get_interfaces(Some(interface_config))
        .expect("Failed to get interfaces");

    let mut interface = interfaces
        .iter()
        .max_by_key(|k| ProviderPrio(k.provider.clone()))
        .ok_or(Error::Other("No suitable interface found".to_string()))?;
    interface.set_endpoint_address(EndpointAddress {
        node: cli.node.as_deref(),
        service: cli.service.as_deref(),
    });

    tracing::info!(
        provider = ?interface.provider,
        node = ?interface.endpoint_address.node,
        service = ?interface.endpoint_address.service,
        caps = ?interface.caps,
        "Selected interface");

    match &cli.command {
        Command::Initiator {
            flow_id,
            target_info,
        } => {
            let initiator =
                InitiatorEndpoint::new(&instance, fabrics_instance, interface, flow_id)?;
            initiator.run(target_info, running)?;
        }
        Command::Target {
            flow_file,
            target_info_path,
        } => {
            let (target, target_info) =
                TargetEndpoint::new(&instance, &fabrics_instance, interface, flow_file)?;

            if let Some(target_info_file) = target_info_path
                && target_info_file.starts_with('@')
            {
                let file = if target_info_file.starts_with("@") {
                    target_info_file
                        .strip_prefix('@')
                        .expect("impossible to fail.") //SAFETY: we already checked that the string starts with '@';
                } else {
                    target_info_file.as_str()
                };
                std::fs::write(file, target_info.to_string()?.as_bytes())?;
            }
            tracing::info!(
                "Target Info: {}",
                BASE64_STANDARD.encode(target_info.to_string()?)
            );
            target.run(running)?;
        }
    }

    Ok(())
}
