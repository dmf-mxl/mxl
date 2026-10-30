// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::FlowWriter;

use crate::fabrics::InterfaceConfig;
use mxl_sys::types::{FabricsInterfaceConfig, FabricsTargetConfig, MXL_FABRICS_API_VERSION};

/// Configuration object required to set up a target.
pub struct Config<'a> {
    version: i32,
    interface: InterfaceConfig,
    pub(crate) flow_writer: &'a FlowWriter,
}

impl<'a> Config<'a> {
    pub fn new(interface: InterfaceConfig, flow_writer: &'a FlowWriter) -> Self {
        Self {
            version: MXL_FABRICS_API_VERSION as i32,
            interface,
            flow_writer,
        }
    }
}
impl<'a> TryFrom<&Config<'a>> for FabricsTargetConfig {
    type Error = crate::Error;

    fn try_from(value: &Config) -> Result<Self, Self::Error> {
        Ok(Self {
            version: value.version,
            interface: FabricsInterfaceConfig::try_from(&value.interface)?,
            writer: value.flow_writer.inner(),
        })
    }
}
