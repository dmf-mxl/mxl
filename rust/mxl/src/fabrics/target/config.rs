// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::FlowWriter;

use crate::Error;
use crate::fabrics::{InterfaceConfig, interface::config::OwnedInterfaceConfig};

/// Configuration object required to set up a target.
pub struct Config<'a> {
    version: i32,
    interface: InterfaceConfig<'a>,
    pub(crate) flow_writer: &'a FlowWriter,
}

impl<'a> Config<'a> {
    pub fn new(interface: InterfaceConfig<'a>, flow_writer: &'a FlowWriter) -> Self {
        Self {
            version: 0,
            interface,
            flow_writer,
        }
    }
}

pub(crate) struct OwnedTargetConfig {
    inner: mxl_sys::fabrics::FabricsTargetConfig,
    _interface: OwnedInterfaceConfig,
}

impl OwnedTargetConfig {
    pub(crate) fn new(value: &Config<'_>) -> Result<Self, Error> {
        let interface = OwnedInterfaceConfig::new(&value.interface)?;

        Ok(Self {
            inner: mxl_sys::fabrics::FabricsTargetConfig {
                version: value.version,
                interface: *interface.as_ffi(),
                // SAFETY: Both types are equivalent opaque writer handles from different bindgen modules.
                writer: value.flow_writer.inner().cast(),
            },
            _interface: interface,
        })
    }

    pub(crate) fn as_ffi(&self) -> &mxl_sys::fabrics::FabricsTargetConfig {
        &self.inner
    }
}
