// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::sync::Arc;

use super::write_access::GrainWriteAccess;

use crate::{Error, Result, instance::InstanceContext, writer::FlowWriterInstance};

/// MXL Flow Writer for discrete flows (grain-based data like video frames)
pub struct GrainWriter {
    context: Arc<InstanceContext>,
    writer: Arc<FlowWriterInstance>,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for GrainWriter {}

impl GrainWriter {
    pub(crate) fn new(context: Arc<InstanceContext>, writer: Arc<FlowWriterInstance>) -> Self {
        Self { context, writer }
    }

    #[deprecated(
        since = "0.2.0",
        note = "The MXL FlowWriter lifetime is now automatically managed internally. You should not be calling destroy() on it anymore. This function is now a no-op and will be removed in a future version."
    )]
    pub fn destroy(self) -> Result<()> {
        Ok(())
    }

    /// The current MXL implementation states a TODO to allow multiple grains to be edited at the
    /// same time. For this reason, there is no protection on the Rust level against trying to open
    /// multiple grains. If the TODO ever gets removed, it may be worth considering pattern where
    /// opening grain would consume the writer and then return it back on commit or cancel.
    pub fn open_grain<'a>(&'a self, index: u64) -> Result<GrainWriteAccess<'a>> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut payload_ptr: *mut u8 = std::ptr::null_mut();
        unsafe {
            Error::from_status(self.context.api.flow_writer_open_grain(
                self.writer.as_ptr(),
                index,
                &mut grain_info,
                &mut payload_ptr,
            ))?;
        }

        if payload_ptr.is_null() {
            return Err(Error::Other(format!(
                "Failed to open grain payload for index {index}.",
            )));
        }

        Ok(GrainWriteAccess::new(
            self.context.clone(),
            self.writer.as_ref(),
            grain_info,
            payload_ptr,
        ))
    }
}
