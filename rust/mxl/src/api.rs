// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{path::Path, sync::Arc};

use mxl_sys::libmxl;

use crate::Result;

pub type MxlApi = libmxl;
pub type MxlApiHandle = Arc<MxlApi>;

pub fn load_api(path_to_so_file: impl AsRef<Path>) -> Result<MxlApiHandle> {
    Ok(Arc::new(unsafe {
        libmxl::new(path_to_so_file.as_ref().as_os_str())?
    }))
}

#[cfg(feature = "mxl-fabrics-ofi")]
pub type MxlFabricsApi = mxl_sys::fabrics::libmxlfabrics;
#[cfg(feature = "mxl-fabrics-ofi")]
pub type MxlFabricsAPiHandle = Arc<MxlFabricsApi>;

#[cfg(feature = "mxl-fabrics-ofi")]
pub fn load_fabrics_api(path_to_so_file: impl AsRef<Path>) -> Result<MxlFabricsAPiHandle> {
    Ok(Arc::new(unsafe {
        mxl_sys::fabrics::libmxlfabrics::new(path_to_so_file.as_ref().as_os_str())?
    }))
}
