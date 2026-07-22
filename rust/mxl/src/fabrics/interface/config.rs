use crate::{
    Error,
    fabrics::{
        EndpointAddress, capabilities::Capabilities, endpoint_address::OwnedEndpointAddress,
        provider::ProviderType,
    },
};

use std::ffi::CString;

pub struct InterfaceConfigBuilder<'a> {
    provider: Option<ProviderType>,
    caps: Option<Capabilities>,
    endpoint_address: EndpointAddress<'a>,
    attr: Option<&'a str>,
}

impl<'a> InterfaceConfigBuilder<'a> {
    pub(crate) fn new(endpoint_address: EndpointAddress<'a>) -> Self {
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
    pub fn build(self) -> InterfaceConfig<'a> {
        InterfaceConfig {
            provider: self.provider.unwrap_or(ProviderType::Any),
            caps: self.caps.unwrap_or_default(),
            endpoint_address: self.endpoint_address,
            attr: self.attr,
        }
    }
}

/// A configuration for a network interface, including the provider type, capabilities, endpoint address, and optional attributes.
#[derive(Debug)]
pub struct InterfaceConfig<'a> {
    pub provider: ProviderType,
    pub caps: Capabilities,
    pub endpoint_address: EndpointAddress<'a>,
    pub attr: Option<&'a str>,
}
impl<'a> InterfaceConfig<'a> {
    pub fn builder(endpoint_address: EndpointAddress<'a>) -> InterfaceConfigBuilder<'a> {
        InterfaceConfigBuilder::new(endpoint_address)
    }
    pub fn set_endpoint_address(&mut self, endpoint_address: EndpointAddress<'a>) {
        self.endpoint_address = endpoint_address;
    }
}
impl TryFrom<&InterfaceConfig<'_>> for OwnedInterfaceConfig {
    type Error = Error;
    fn try_from(value: &InterfaceConfig<'_>) -> Result<Self, Self::Error> {
        OwnedInterfaceConfig::new(value)
    }
}

/// A wrapper around `mxl_sys::fabrics::FabricsInterfaceConfig` that owns the memory for the endpoint address and attribute strings.
pub(crate) struct OwnedInterfaceConfig {
    inner: mxl_sys::fabrics::FabricsInterfaceConfig,
    _address: OwnedEndpointAddress,
    _attr: Option<CString>,
}

impl OwnedInterfaceConfig {
    pub(crate) fn new(value: &InterfaceConfig<'_>) -> Result<Self, Error> {
        let address = OwnedEndpointAddress::new(&value.endpoint_address)?;
        let attr = value.attr.map(CString::new).transpose()?;

        Ok(Self {
            inner: mxl_sys::fabrics::FabricsInterfaceConfig {
                version: mxl_sys::fabrics::MXL_FABRICS_API_VERSION as i32,
                provider: (&value.provider).into(),
                caps: (&value.caps).into(),
                address: address.as_ffi(),
                attr: attr
                    .as_ref()
                    .map_or(std::ptr::null_mut(), |value| value.as_ptr() as *mut i8),
            },
            _address: address,
            _attr: attr,
        })
    }

    pub(crate) fn as_ffi(&self) -> &mxl_sys::fabrics::FabricsInterfaceConfig {
        &self.inner
    }
}
impl<'a> TryFrom<mxl_sys::fabrics::FabricsInterfaceConfig> for InterfaceConfig<'a> {
    type Error = crate::Error;
    fn try_from(value: mxl_sys::fabrics::FabricsInterfaceConfig) -> Result<Self, Self::Error> {
        let provider = (value.provider as mxl_sys::fabrics::FabricsProvider).into();
        let caps = value.caps.into();
        let endpoint_address = EndpointAddress {
            node: (!value.address.node.is_null())
                .then(|| unsafe { std::ffi::CStr::from_ptr(value.address.node) }.to_str())
                .transpose()
                .map_err(|e| Error::Other(e.to_string()))?,
            service: (!value.address.service.is_null())
                .then(|| unsafe { std::ffi::CStr::from_ptr(value.address.service) }.to_str())
                .transpose()
                .map_err(|e| Error::Other(e.to_string()))?,
        };

        let attr = (!value.attr.is_null())
            .then(|| unsafe { std::ffi::CStr::from_ptr(value.attr) }.to_str())
            .transpose()
            .map_err(|e| Error::Other(e.to_string()))?;

        Ok(Self {
            provider,
            caps,
            endpoint_address,
            attr,
        })
    }
}
