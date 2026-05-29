//! This module provides the Fabrics API extension for this library. The main type is the
//! [FabricsInstance], which is used to create Targets and Initiators for remote data transfers.
//! This module is gated by the `mxl-fabrics-ofi` feature flag.
//!
//! # Details
//! - To get a FabricsInstance, you must create it from MXL instance and a loaded Fabrics API.
//! ```
//! let mxl_api = mxl::load_api(mxl::config::get_mxl_so_path()) .unwrap();
//! let instance = mxl::MxlInstance::new(mxl_api, "/dev/shm","").unwrap();
//!
//! let mxl_fabrics_api = mxl::load_fabrics_api(mxl::config::get_mxl_fabrics_ofi_so_path());
//! let fabrics_instance = instance.create_fabrics_instance(&mxl_fabrics_api.unwrap()).unwrap();
//!
//! // You can now create Targets and Initiators from the fabrics_instance
//! let target = fabrics_instance.create_target().unwrap();
//! let initiator = fabrics_instance.create_initiator().unwrap();
//! ````
mod config;
pub mod initiator;
mod instance;
mod provider;
pub mod target;
mod target_info;

pub use config::EndpointAddress;
// pub use initiator::{Config as InitiatorConfig, Either as InitiatorEither, Initiator};
pub use instance::FabricsInstance;
pub use provider::Provider;
// pub use target::{Config as TargetConfig, Target};
pub use target_info::TargetInfo;

pub(crate) use instance::create_instance;
