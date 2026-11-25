// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{path::Path, sync::Arc};

use mxl_sys::libmxl;

use crate::{Error, Result};

pub type MxlApi = libmxl;
pub type MxlApiHandle = Arc<MxlApi>;

pub fn load_api(path_to_so_file: impl AsRef<Path>) -> Result<MxlApiHandle> {
    Ok(Arc::new(unsafe {
        libmxl::new(path_to_so_file.as_ref().as_os_str())?
    }))
}

/// Semantic version of the loaded MXL library.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Version {
    pub major: u16,
    pub minor: u16,
    pub bugfix: u16,
    pub build: u16,
    /// Full version string, e.g. "1.2.0+3 g<hash>".
    pub full: String,
}

/// Reads the version of the loaded MXL library. Does not need an instance.
pub fn get_version(api: &MxlApiHandle) -> Result<Version> {
    let mut raw = mxl_sys::VersionType::default();
    unsafe {
        Error::from_status(api.get_version(&mut raw))?;
    }
    let full = if raw.full.is_null() {
        String::new()
    } else {
        unsafe { std::ffi::CStr::from_ptr(raw.full) }
            .to_string_lossy()
            .into_owned()
    };
    Ok(Version {
        major: raw.major,
        minor: raw.minor,
        bugfix: raw.bugfix,
        build: raw.build,
        full,
    })
}

/// Checks whether `path` is on a RAM-backed filesystem (tmpfs/ramfs on Linux,
/// the filesystem type reported by statfs on macOS). Always false on other
/// platforms.
pub fn is_tmp_fs(api: &MxlApiHandle, path: &str) -> Result<bool> {
    let c_path = std::ffi::CString::new(path)?;
    let mut is_tmp_fs = false;
    unsafe {
        Error::from_status(api.is_tmp_fs(c_path.as_ptr(), &mut is_tmp_fs))?;
    }
    Ok(is_tmp_fs)
}

#[cfg(feature = "mxl-fabrics-ofi")]
pub type MxlFabricsApi = mxl_sys::fabrics::libmxlfabrics;
#[cfg(feature = "mxl-fabrics-ofi")]
pub type MxlFabricsApiHandle = Arc<MxlFabricsApi>;

#[cfg(feature = "mxl-fabrics-ofi")]
pub fn load_fabrics_api(path_to_so_file: impl AsRef<Path>) -> Result<MxlFabricsApiHandle> {
    Ok(Arc::new(unsafe {
        mxl_sys::fabrics::libmxlfabrics::new(path_to_so_file.as_ref().as_os_str())?
    }))
}
