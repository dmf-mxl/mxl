// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::{
    Error, FlowReader,
    fabrics::{config::EndpointAddress, provider::Provider},
};

/// Configuration object required to set up an initiator.
pub struct Config<'a> {
    version: i32,
    endpoint_addr: EndpointAddress<'a>,
    provider: Provider,
    flow_reader: &'a FlowReader,
}

impl<'a> Config<'a> {
    pub fn new(
        endpoint_addr: EndpointAddress<'a>,
        provider: Provider,
        flow_reader: &'a FlowReader,
    ) -> Self {
        Self {
            version: 0,
            endpoint_addr,
            provider,
            flow_reader,
        }
    }
}

impl<'a> TryFrom<&Config<'a>> for mxl_sys::fabrics::FabricsInitiatorConfig {
    type Error = Error;

    fn try_from(value: &Config) -> Result<Self, Self::Error> {
        Ok(Self {
            version: value.version,
            endpointAddress: (&value.endpoint_addr).try_into()?,
            provider: (&value.provider).into(),
            // SAFETY: The type cast is necessary, because this FlowReader is scoped in mxl_sys::fabrics::*, not mxl_sys::*, but this is the same type.
            reader: unsafe {
                std::mem::transmute::<mxl_sys::FlowReader, mxl_sys::fabrics::FlowReader>(
                    value.flow_reader.inner(),
                )
            },
        })
    }
}
