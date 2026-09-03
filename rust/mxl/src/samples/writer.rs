// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::sync::Arc;

use crate::{
    Error, Result, SamplesWriteAccess, instance::InstanceContext, writer::FlowWriterInstance,
};

/// MXL Flow Writer for continuous flows (samples-based data like audio)
pub struct SamplesWriter {
    context: Arc<InstanceContext>,
    writer: Arc<FlowWriterInstance>,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for SamplesWriter {}

impl SamplesWriter {
    pub(crate) fn new(context: Arc<InstanceContext>, writer: Arc<FlowWriterInstance>) -> Self {
        Self { context, writer }
    }
    #[deprecated(
        since = "0.2.0",
        note = "The MXL FlowWriter lifetime is now automatically managed internally. You should not be calling destroy() on it anymore. This function is a no-op and will be removed in a future version."
    )]
    pub fn destroy(self) -> Result<()> {
        Ok(())
    }

    pub fn open_samples<'a>(&'a self, index: u64, count: usize) -> Result<SamplesWriteAccess<'a>> {
        let mut buffer_slice: mxl_sys::MutableWrappedMultiBufferSlice =
            unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(self.context.api.flow_writer_open_samples(
                self.writer.as_ptr(),
                index,
                count,
                &mut buffer_slice,
            ))?;
        }
        Ok(SamplesWriteAccess::new(
            self.context.clone(),
            self.writer.as_ref(),
            buffer_slice,
        ))
    }
}
