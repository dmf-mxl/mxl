use std::sync::Arc;

use crate::{
    Error, Result, SamplesData,
    flow::{FlowInfo, reader::get_flow_info},
    instance::InstanceContext,
};

pub struct SamplesReader {
    context: Arc<InstanceContext>,
    reader: mxl_sys::mxlFlowReader,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for SamplesReader {}

impl SamplesReader {
    pub(crate) fn new(context: Arc<InstanceContext>, reader: mxl_sys::mxlFlowReader) -> Self {
        Self { context, reader }
    }

    pub fn destroy(mut self) -> Result<()> {
        self.destroy_inner()
    }

    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.context, self.reader)
    }

    pub fn get_samples(&self, index: u64, count: usize) -> Result<SamplesData> {
        let mut buffer_slice: mxl_sys::WrappedMultiBufferSlice = unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(self.context.api.mxl_flow_reader_get_samples(
                self.reader,
                index,
                count,
                &mut buffer_slice,
            ))?;
        }
        Ok(SamplesData::new(buffer_slice))
    }

    fn destroy_inner(&mut self) -> Result<()> {
        if self.reader.is_null() {
            return Err(Error::InvalidArg);
        }

        let mut reader = std::ptr::null_mut();
        std::mem::swap(&mut self.reader, &mut reader);

        Error::from_status(unsafe {
            self.context
                .api
                .mxl_release_flow_reader(self.context.instance, reader)
        })
    }
}

impl Drop for SamplesReader {
    fn drop(&mut self) {
        if !self.reader.is_null() {
            if let Err(err) = self.destroy_inner() {
                tracing::error!("Failed to release MXL flow reader (continuous): {:?}", err);
            }
        }
    }
}
