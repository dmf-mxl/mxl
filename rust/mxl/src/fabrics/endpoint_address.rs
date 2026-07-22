// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::ffi::CString;

use crate::Error;

/// Address of a logical network endpoint. This is analogous to a hostname and port number in classic ipv4 networking.
/// The actual values for node and service vary between providers, but often an ip address as the node value and a port number as the service
/// value are sufficient.
#[derive(Debug)]
pub struct EndpointAddress<'a> {
    pub node: Option<&'a str>,
    pub service: Option<&'a str>,
}

/// A wrapper around the FFI representation of an EndpointAddress, which owns the underlying CStrings for node and service.
pub(crate) struct OwnedEndpointAddress {
    inner: mxl_sys::fabrics::FabricsEndpointAddress,
    _node: Option<CString>,
    _service: Option<CString>,
}

impl OwnedEndpointAddress {
    pub(crate) fn new(value: &EndpointAddress<'_>) -> Result<Self, Error> {
        let node = value.node.map(CString::new).transpose()?;
        let service = value.service.map(CString::new).transpose()?;

        Ok(Self {
            inner: mxl_sys::fabrics::FabricsEndpointAddress {
                node: node
                    .as_ref()
                    .map_or(std::ptr::null_mut(), |value| value.as_ptr() as *mut i8),
                service: service
                    .as_ref()
                    .map_or(std::ptr::null_mut(), |value| value.as_ptr() as *mut i8),
            },
            _node: node,
            _service: service,
        })
    }

    pub(crate) fn as_ffi(&self) -> mxl_sys::fabrics::FabricsEndpointAddress {
        self.inner
    }
}
