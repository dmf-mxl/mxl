// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::{
    Error, FlowReader,
    fabrics::{InterfaceConfig, interface::config::OwnedInterfaceConfig},
};

/// Configuration object required to set up an initiator.
pub struct Config<'a> {
    version: i32,
    interface: InterfaceConfig<'a>,
    pub(crate) flow_reader: &'a FlowReader,
}

impl<'a> Config<'a> {
    pub fn new(interface: InterfaceConfig<'a>, flow_reader: &'a FlowReader) -> Self {
        Self {
            version: 0,
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
            interface: *OwnedInterfaceConfig::try_from(&value.interface)?.as_ffi(),
            // SAFETY: The type cast is necessary, because this FlowReader is scoped in mxl_sys::fabrics::*, not mxl_sys::*, but this is the same type.
            reader: unsafe {
                std::mem::transmute::<mxl_sys::FlowReader, mxl_sys::fabrics::FlowReader>(
                    value.flow_reader.inner(),
                )
            },
        })
    }
}

pub(crate) struct OwnedInitiatorConfig {
    inner: mxl_sys::fabrics::FabricsInitiatorConfig,
    _interface: OwnedInterfaceConfig,
}

impl OwnedInitiatorConfig {
    pub(crate) fn new(value: &Config<'_>) -> Result<Self, Error> {
        let interface = OwnedInterfaceConfig::new(&value.interface)?;

        Ok(Self {
            inner: mxl_sys::fabrics::FabricsInitiatorConfig {
                version: value.version,
                interface: *interface.as_ffi(),
                // SAFETY: Both types are equivalent opaque reader handles from different bindgen modules.
                reader: value.flow_reader.inner().cast(),
            },
            _interface: interface,
        })
    }

    pub(crate) fn as_ffi(&self) -> &mxl_sys::fabrics::FabricsInitiatorConfig {
        &self.inner
    }
}
