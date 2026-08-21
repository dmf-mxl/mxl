// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::ffi::{CStr, CString};

use mxl_sys::fabrics::FabricsEndpointAddress;

use crate::Error;

/// Address of a logical network endpoint. This is analogous to a hostname and port number in classic ipv4 networking.
/// The actual values for node and service vary between providers, but often an ip address as the node value and a port number as the service
/// value are sufficient.
#[derive(Debug)]
pub struct EndpointAddress {
    pub node: Option<CString>,
    pub service: Option<CString>,
}
impl EndpointAddress {
    pub fn new(node: Option<&str>, service: Option<&str>) -> Result<Self, Error> {
        // Here we can't guarantee that &str is null terminated, so need to convert to CString first,
        // which will ensure include null termination and provide an owned value of the
        // null-terminated string.
        let node = node.map(CString::new).transpose()?;
        let service = service.map(CString::new).transpose()?;

        Ok(Self { node, service })
    }
    pub fn node(&self) -> Result<Option<&str>, crate::Error> {
        self.node
            .as_ref()
            .map(|s| {
                s.to_str()
                    .map_err(|_| crate::Error::Other("Invalid UTF-8".to_string()))
            })
            .transpose()
    }
    pub fn service(&self) -> Result<Option<&str>, crate::Error> {
        self.service
            .as_ref()
            .map(|s| {
                s.to_str()
                    .map_err(|_| crate::Error::Other("Invalid UTF-8".to_string()))
            })
            .transpose()
    }
}
impl From<&EndpointAddress> for FabricsEndpointAddress {
    fn from(value: &EndpointAddress) -> Self {
        let node = value.node.as_ref().map_or(std::ptr::null(), |s| s.as_ptr());
        let service = value
            .service
            .as_ref()
            .map_or(std::ptr::null(), |s| s.as_ptr());
        Self { node, service }
    }
}
impl From<&FabricsEndpointAddress> for EndpointAddress {
    fn from(value: &FabricsEndpointAddress) -> Self {
        let node = if value.node.is_null() {
            None
        } else {
            //SAFETY: if the pointer is not null, then we assume the FFI layer has allocated a valid C string and we can safely convert it to a CString.
            unsafe { Some(CStr::from_ptr(value.node as *mut i8).to_owned()) }
        };
        let service = if value.service.is_null() {
            None
        } else {
            //SAFETY: if the pointer is not null, then we assume the FFI layer has allocated a valid C string and we can safely convert it to a CString.
            unsafe { Some(CStr::from_ptr(value.service as *mut i8).to_owned()) }
        };
        Self { node, service }
    }
}
