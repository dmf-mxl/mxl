// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{cell::Cell, marker::PhantomData, sync::Arc};

use crate::{
    Error, Result, SamplesWriteAccess,
    writer::{FlowWriterResource, FlowWriterResourceKeepAlive},
};

/// MXL Flow Writer for continuous flows (samples-based data like audio)
pub struct SamplesWriter {
    writer: Arc<FlowWriterResource>,
    _not_sync: PhantomData<Cell<()>>, // Prevent Sync implementation, as the underlying MXL writer
                                      // is not thread-safe
}

impl SamplesWriter {
    pub(crate) fn new(writer: Arc<FlowWriterResource>) -> Self {
        Self {
            writer,
            _not_sync: PhantomData,
        }
    }

    #[allow(dead_code)]
    pub(crate) fn keep_alive(&self) -> FlowWriterResourceKeepAlive {
        self.writer.keep_alive()
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
