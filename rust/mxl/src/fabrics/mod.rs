// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

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
mod capabilities;
mod endpoint_address;
pub mod initiator;
mod instance;
mod interface;
mod provider;
pub mod target;
mod target_info;

pub use capabilities::Capabilities;
pub use endpoint_address::EndpointAddress;
pub use instance::FabricsInstance;
pub use interface::config::InterfaceConfig;
pub use provider::{Provider, ProviderType};
pub use target_info::TargetInfo;

pub(crate) use instance::create_instance;
