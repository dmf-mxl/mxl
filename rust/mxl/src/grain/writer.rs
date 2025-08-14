use std::sync::Arc;

use super::write_access::GrainWriteAccess;

use crate::{instance::InstanceContext, Error, Result};

/// MXL Flow Writer for discrete flows (grain-based data like video frames)
pub struct GrainWriter {
    context: Arc<InstanceContext>,
    writer: mxl_sys::mxlFlowWriter,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for GrainWriter {}

impl GrainWriter {
    pub(crate) fn new(context: Arc<InstanceContext>, writer: mxl_sys::mxlFlowWriter) -> Self {
        Self { context, writer }
    }

    pub fn destroy(mut self) -> Result<()> {
        self.destroy_inner()
    }

    /// The current MXL implementation states a TODO to allow multiple grains to be edited at the
    /// same time. For this reason, there is no protection on the Rust level against trying to open
    /// multiple grains. If the TODO ever gets removed, it may be worth considering pattern where
    /// opening grain would consume the writer and then return it back on commit or cancel.
    pub fn open_grain<'a>(&'a self, index: u64) -> Result<GrainWriteAccess<'a>> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut payload_ptr: *mut u8 = std::ptr::null_mut();
        unsafe {
            Error::from_status(self.context.api.mxl_flow_writer_open_grain(
                self.writer,
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
            self.writer,
            grain_info,
            payload_ptr,
        ))
    }

    fn destroy_inner(&mut self) -> Result<()> {
        if self.writer.is_null() {
            return Err(Error::InvalidArg);
        }

        let mut writer = std::ptr::null_mut();
        std::mem::swap(&mut self.writer, &mut writer);

        Error::from_status(unsafe {
            self.context
                .api
                .mxl_release_flow_writer(self.context.instance, writer)
        })
    }
}

impl Drop for GrainWriter {
    fn drop(&mut self) {
        if !self.writer.is_null() {
            if let Err(err) = self.destroy_inner() {
                tracing::error!("Failed to release MXL flow writer (discrete): {:?}", err);
            }
        }
    }
}
