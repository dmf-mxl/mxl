use std::ffi::CString;

use crate::Error;

/// Address of a logical network endpoint. This is analogous to a hostname and port number in classic ipv4 networking.
/// The actual values for node and service vary between providers, but often an ip address as the node value and a port number as the service
/// value are sufficient.
pub struct EndpointAddress<'a> {
    pub node: Option<&'a str>,
    pub service: Option<&'a str>,
}

impl<'a> TryFrom<&EndpointAddress<'a>> for mxl_sys::fabrics::FabricsEndpointAddress {
    type Error = Error;

    fn try_from(value: &EndpointAddress) -> Result<Self, Self::Error> {
        let node = if let Some(node) = value.node {
            CString::new(node)?.into_raw()
        } else {
            std::ptr::null_mut()
        };
        let service = if let Some(service) = value.service {
            CString::new(service)?.into_raw()
        } else {
            std::ptr::null_mut()
        };
        Ok(mxl_sys::fabrics::FabricsEndpointAddress { node, service })
    }
}
