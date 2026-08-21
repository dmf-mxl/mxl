// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::FlowWriter;

use crate::fabrics::InterfaceConfig;

/// Configuration object required to set up a target.
pub struct Config<'a> {
    version: i32,
    interface: InterfaceConfig,
    pub(crate) flow_writer: &'a FlowWriter,
}

impl<'a> Config<'a> {
    pub fn new(interface: InterfaceConfig, flow_writer: &'a FlowWriter) -> Self {
        Self {
            version: mxl_sys::fabrics::MXL_FABRICS_API_VERSION as i32,
            interface,
            flow_writer,
        }
    }
}
impl<'a> TryFrom<&Config<'a>> for mxl_sys::fabrics::FabricsTargetConfig {
    type Error = crate::Error;

    fn try_from(value: &Config) -> Result<Self, Self::Error> {
        Ok(Self {
            version: value.version,
            interface: mxl_sys::fabrics::FabricsInterfaceConfig::try_from(&value.interface)?,
            // SAFETY: Both types are equivalent opaque writer handles from different bindgen modules.
            writer: value.flow_writer.inner().cast(),
        })
    }
}
