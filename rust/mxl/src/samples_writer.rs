use std::sync::Arc;

use crate::samples_write_access::SamplesWriteAccess;
use crate::{Error, Result, instance::InstanceContext};

/// MXL Flow Writer for continuous flows (samples-based data like audio)
pub struct SamplesWriter {
    context: Arc<InstanceContext>,
    writer: mxl_sys::mxlFlowWriter,
}

impl SamplesWriter {
    pub(crate) fn new(context: Arc<InstanceContext>, writer: mxl_sys::mxlFlowWriter) -> Self {
        Self { context, writer }
    }

    pub fn destroy(mut self) -> Result<()> {
        self.destroy_inner()
    }

    pub fn open_samples<'a>(&'a self, index: u64, count: usize) -> Result<SamplesWriteAccess<'a>> {
        let mut buffer_slice: mxl_sys::MutableWrappedMultiBufferSlice =
            unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(self.context.api.mxl_flow_writer_open_samples(
                self.writer,
                index,
                count,
                &mut buffer_slice,
            ))?;
        }
        Ok(SamplesWriteAccess::new(
            self.context.clone(),
            self.writer,
            buffer_slice,
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

impl Drop for SamplesWriter {
    fn drop(&mut self) {
        if !self.writer.is_null() {
            if let Err(err) = self.destroy_inner() {
                tracing::error!("Failed to release MXL flow writer (continuous): {:?}", err);
            }
        }
    }
}
