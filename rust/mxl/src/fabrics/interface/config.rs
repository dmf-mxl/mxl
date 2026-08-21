// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::{
    Error,
    fabrics::{EndpointAddress, capabilities::Capabilities, provider::ProviderType},
};

use std::ffi::CString;

pub struct InterfaceConfigBuilder<'a> {
    provider: Option<ProviderType>,
    caps: Option<Capabilities>,
    endpoint_address: EndpointAddress,
    attr: Option<&'a str>,
}

impl<'a> InterfaceConfigBuilder<'a> {
    pub(crate) fn new(endpoint_address: EndpointAddress) -> Self {
        Self {
            provider: None,
            caps: None,
            endpoint_address,
            attr: None,
        }
    }

    /// Sets the provider type for the interface configuration.
    pub fn provider(mut self, provider: ProviderType) -> Self {
        self.provider = Some(provider);
        self
    }

    /// Sets the capabilities for the interface configuration.
    pub fn caps(mut self, caps: Capabilities) -> Self {
        self.caps = Some(caps);
        self
    }

    pub fn attr(mut self, attr: &'a str) -> Self {
        self.attr = Some(attr);
        self
    }

    /// Builds the `InterfaceConfig`
    pub fn build(self) -> Result<InterfaceConfig, crate::Error> {
        Ok(InterfaceConfig {
            provider: self.provider.unwrap_or(ProviderType::Any),
            caps: self.caps.unwrap_or_default(),
            endpoint_address: self.endpoint_address,
            attr: self.attr.map(CString::new).transpose()?,
        })
    }
}

/// A configuration for a network interface, including the provider type, capabilities, endpoint address, and optional attributes.
#[derive(Debug)]
pub struct InterfaceConfig {
    pub provider: ProviderType,
    pub caps: Capabilities,
    pub endpoint_address: EndpointAddress,
    pub attr: Option<CString>,
}
impl<'a> InterfaceConfig {
    pub fn builder(endpoint_address: EndpointAddress) -> InterfaceConfigBuilder<'a> {
        InterfaceConfigBuilder::new(endpoint_address)
    }
    pub fn set_endpoint_address(&mut self, endpoint_address: EndpointAddress) {
        self.endpoint_address = endpoint_address;
    }
}
impl TryFrom<&InterfaceConfig> for mxl_sys::fabrics::FabricsInterfaceConfig {
    type Error = Error;
    fn try_from(value: &InterfaceConfig) -> Result<Self, Self::Error> {
        Ok(mxl_sys::fabrics::FabricsInterfaceConfig {
            version: mxl_sys::fabrics::MXL_FABRICS_API_VERSION as i32,
            provider: (&value.provider).into(),
            caps: (&value.caps).into(),
            address: (&value.endpoint_address).into(),
            attr: value
                .attr
                .as_ref()
                .map_or(std::ptr::null_mut(), |v| v.as_ptr()),
        })
    }
}
impl TryFrom<mxl_sys::fabrics::FabricsInterfaceConfig> for InterfaceConfig {
    type Error = crate::Error;
    fn try_from(value: mxl_sys::fabrics::FabricsInterfaceConfig) -> Result<Self, Self::Error> {
        let provider = (value.provider as mxl_sys::fabrics::FabricsProvider).try_into()?;
        let caps = value.caps.into();
        let endpoint_address = EndpointAddress::from(&value.address);
        let attr =
            (!value.attr.is_null()).then(|| unsafe { CString::from_raw(value.attr as *mut i8) });

        Ok(Self {
            provider,
            caps,
            endpoint_address,
            attr,
        })
    }
}
