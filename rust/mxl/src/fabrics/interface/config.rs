use crate::{
    Error,
    fabrics::{EndpointAddress, capabilities::Capabilities, provider::ProviderType},
};

pub struct InterfaceConfigBuilder<'a> {
    provider: Option<ProviderType>,
    caps: Option<Capabilities>,
    endpoint_address: EndpointAddress<'a>,
    attr: Option<&'a str>,
}

impl<'a> InterfaceConfigBuilder<'a> {
    pub fn new(endpoint_address: EndpointAddress<'a>) -> Self {
        Self {
            provider: None,
            caps: None,
            endpoint_address,
            attr: None,
        }
    }

    pub fn provider(mut self, provider: ProviderType) -> Self {
        self.provider = Some(provider);
        self
    }

    pub fn caps(mut self, caps: Capabilities) -> Self {
        self.caps = Some(caps);
        self
    }

    pub fn attr(mut self, attr: &'a str) -> Self {
        self.attr = Some(attr);
        self
    }

    pub fn build(self) -> Result<InterfaceConfig<'a>, Error> {
        Ok(InterfaceConfig {
            provider: self.provider.unwrap_or(ProviderType::Any),
            caps: self.caps.unwrap_or_default(),
            endpoint_address: self.endpoint_address,
            attr: self.attr,
        })
    }
}

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
impl<'a> TryFrom<&InterfaceConfig<'a>> for mxl_sys::fabrics::FabricsInterfaceConfig {
    type Error = crate::Error;

    fn try_from(value: &InterfaceConfig) -> Result<Self, Self::Error> {
        Ok(Self {
            version: mxl_sys::fabrics::MXL_FABRICS_API_VERSION as i32,
            provider: (&value.provider).into(),
            caps: (&value.caps).into(),
            address: (&value.endpoint_address).try_into()?,
            attr: if let Some(attr) = value.attr {
                std::ffi::CString::new(attr)?.into_raw()
            } else {
                std::ptr::null_mut()
            },
        })
    }
}
impl<'a> TryFrom<InterfaceConfig<'a>> for mxl_sys::fabrics::FabricsInterfaceConfig {
    type Error = crate::Error;

    fn try_from(value: InterfaceConfig) -> Result<Self, Self::Error> {
        (&value).try_into()
    }
}
impl<'a> TryFrom<mxl_sys::fabrics::FabricsInterfaceConfig> for InterfaceConfig<'a> {
    type Error = crate::Error;
    fn try_from(value: mxl_sys::fabrics::FabricsInterfaceConfig) -> Result<Self, Self::Error> {
        let provider = (value.provider as mxl_sys::fabrics::FabricsProvider).into();
        let caps = value.caps.into();
        let endpoint_address = EndpointAddress {
            node: if value.address.node.is_null() {
                None
            } else {
                Some(
                    unsafe { std::ffi::CStr::from_ptr(value.address.node) }
                        .to_str()
                        .map_err(|e| Error::Other(e.to_string()))?,
                )
            },
            service: if value.address.service.is_null() {
                None
            } else {
                Some(
                    unsafe { std::ffi::CStr::from_ptr(value.address.service) }
                        .to_str()
                        .map_err(|e| Error::Other(e.to_string()))?,
                )
            },
        };
        let attr = if value.attr.is_null() {
            None
        } else {
            Some(
                unsafe { std::ffi::CStr::from_ptr(value.attr) }
                    .to_str()
                    .map_err(|e| Error::Other(e.to_string()))?,
            )
        };
        Ok(Self {
            provider,
            caps,
            endpoint_address,
            attr,
        })
    }
}
