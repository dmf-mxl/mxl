// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{ffi::CString, sync::Arc};

use crate::error::{Error, Result};
use mxl_sys::fabrics::FabricsTargetInfo;

use crate::fabrics::instance::FabricsInstanceContext;

/// The TargetInfo object holds the local fabric address, keys and memory region addresses for a target.
/// It is returned after setting up a new target and must be passed to the initiator to connect it.
pub struct TargetInfo {
    ctx: Arc<FabricsInstanceContext>,
    pub(crate) inner: FabricsTargetInfo,
}
unsafe impl Send for TargetInfo {}
/// SAFETY: Although the `FabricsInstanceContext` type as a whole is not thread-safe, the subset of functions that this `TargetInfo` type uses is thread-safe: `fabrics_target_info_from_string` and `fabrics_target_info_to_string`
unsafe impl Sync for TargetInfo {}

impl TargetInfo {
    pub(crate) fn new(ctx: Arc<FabricsInstanceContext>, inner: FabricsTargetInfo) -> Self {
        Self { ctx, inner }
    }

    /// Parse a targetInfo object from its string representation.
    /// Public visibility is set to crate only, because a `FabricsInstanceContext` is required.
    /// See [FabricsInstance](crate::FabricsInstance).
    pub(crate) fn from_str(ctx: Arc<FabricsInstanceContext>, s: &str) -> Result<Self> {
        let mut inner = FabricsTargetInfo::default();

        Error::from_status(unsafe {
            ctx.api()
                .fabrics_target_info_from_string(CString::new(s)?.as_ptr(), &mut inner)
        })?;

        Ok(Self::new(ctx, inner))
    }

    /// Serialize a target info object obtained from mxlFabricsTargetSetup() into a string representation.
    pub fn to_string(&self) -> Result<String> {
        let mut size = 0;
        Error::from_status(unsafe {
            self.ctx.api().fabrics_target_info_to_string(
                self.inner,
                std::ptr::null_mut(),
                &mut size,
            )
        })?;

        // The size returned by `fabrics_target_info_to_string` previous call already includes space for null terminator.
        // Since CString ctor also includes the null terminated character, we must take the size minus 1.
        let out_string = unsafe { CString::from_vec_unchecked(vec![0; size - 1]) };

        Error::from_status(unsafe {
            self.ctx.api().fabrics_target_info_to_string(
                self.inner,
                out_string.as_ptr() as *mut i8,
                &mut size,
            )
        })?;
        out_string
            .into_string()
            .map_err(|e| Error::Other(e.to_string()))
    }
}

impl Drop for TargetInfo {
    fn drop(&mut self) {
        if !self.inner.is_null() {
            unsafe { self.ctx.api().fabrics_free_target_info(self.inner) };
        }
    }
}
