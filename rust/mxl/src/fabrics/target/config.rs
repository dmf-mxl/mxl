use crate::FlowWriter;

use crate::{
    Error,
    fabrics::{config::EndpointAddress, provider::Provider},
};

/// Configuration object required to set up a target.
pub struct Config<'a> {
    version: i32,
    endpoint_addr: EndpointAddress<'a>,
    provider: Provider,
    flow_writer: &'a FlowWriter,
}

impl<'a> Config<'a> {
    pub fn new(
        endpoint_addr: EndpointAddress<'a>,
        provider: Provider,
        flow_writer: &'a FlowWriter,
    ) -> Self {
        Self {
            version: 0,
            endpoint_addr,
            provider,
            flow_writer,
        }
    }
}

impl<'a> TryFrom<&Config<'a>> for mxl_sys::fabrics::FabricsTargetConfig {
    type Error = Error;

    fn try_from(value: &Config) -> Result<Self, Self::Error> {
        Ok(Self {
            version: value.version,
            endpointAddress: (&value.endpoint_addr).try_into()?,
            provider: (&value.provider).into(),
            // SAFETY: The type cast is necessary, because this FlowWriter is scoped in mxl_sys::fabrics::*, not mxl_sys::*
            writer: unsafe {
                std::mem::transmute::<mxl_sys::FlowWriter, mxl_sys::fabrics::FlowWriter>(
                    value.flow_writer.inner(),
                )
            },
        })
    }
}
