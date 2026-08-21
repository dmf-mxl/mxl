// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::{Error, FlowReader, fabrics::InterfaceConfig};

/// Configuration object required to set up an initiator.
pub struct Config<'a> {
    version: i32,
    interface: InterfaceConfig,
    pub(crate) flow_reader: &'a FlowReader,
}

impl<'a> Config<'a> {
    pub fn new(interface: InterfaceConfig, flow_reader: &'a FlowReader) -> Self {
        Self {
            version: mxl_sys::fabrics::MXL_FABRICS_API_VERSION as i32,
            interface,
            flow_reader,
        }
    }
}
impl<'a> TryFrom<&Config<'a>> for mxl_sys::fabrics::FabricsInitiatorConfig {
    type Error = Error;

    fn try_from(value: &Config) -> Result<Self, Self::Error> {
        Ok(Self {
            version: value.version,
            interface: mxl_sys::fabrics::FabricsInterfaceConfig::try_from(&value.interface)?,
            // SAFETY: Both types are equivalent opaque reader handles from different bindgen modules.
            reader: value.flow_reader.inner().cast(),
        })
    }
}
