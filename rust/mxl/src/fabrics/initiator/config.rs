// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::{Error, FlowReader, fabrics::InterfaceConfig};
use mxl_sys::types::{FabricsInitiatorConfig, FabricsInterfaceConfig, MXL_FABRICS_API_VERSION};

/// Configuration object required to set up an initiator.
pub struct Config<'a> {
    version: i32,
    interface: InterfaceConfig,
    pub(crate) flow_reader: &'a FlowReader,
}

impl<'a> Config<'a> {
    pub fn new(interface: InterfaceConfig, flow_reader: &'a FlowReader) -> Self {
        Self {
            version: MXL_FABRICS_API_VERSION as i32,
            interface,
            flow_reader,
        }
    }
}
impl<'a> TryFrom<&Config<'a>> for FabricsInitiatorConfig {
    type Error = Error;

    fn try_from(value: &Config) -> Result<Self, Self::Error> {
        Ok(Self {
            version: value.version,
            interface: FabricsInterfaceConfig::try_from(&value.interface)?,
            reader: value.flow_reader.inner(),
        })
    }
}
