// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::sync::Arc;

use crate::{Error, Result, SamplesWriteAccess, writer::FlowWriterInstance};

/// MXL Flow Writer for continuous flows (samples-based data like audio)
pub struct SamplesWriter {
    writer: Arc<FlowWriterInstance>,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for SamplesWriter {}

impl SamplesWriter {
    pub(crate) fn new(writer: Arc<FlowWriterInstance>) -> Self {
        Self { writer }
    }
    #[deprecated(
        since = "0.2.0",
        note = "Flow writer lifetimes are now managed automatically. This method only consumes the handle and always returns `Ok(())`; the underlying writer is released when the last related handle is dropped."
    )]
    pub fn destroy(self) -> Result<()> {
        Ok(())
    }

    pub fn open_samples<'a>(&'a self, index: u64, count: usize) -> Result<SamplesWriteAccess<'a>> {
        let mut buffer_slice: mxl_sys::MutableWrappedMultiBufferSlice =
            unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(self.writer.context.api.flow_writer_open_samples(
                self.writer.as_ptr(),
                index,
                count,
                &mut buffer_slice,
            ))?;
        }
        Ok(SamplesWriteAccess::new(self.writer.as_ref(), buffer_slice))
    }
}
