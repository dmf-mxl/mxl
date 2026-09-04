// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{sync::Arc, time::Duration};

use crate::{
    Error, Result, SamplesData,
    flow::{
        FlowConfigInfo, FlowInfo,
        reader::{get_config_info, get_flow_info, get_runtime_info},
    },
    reader::FlowReaderInstance,
};

pub struct SamplesReader {
    reader: Arc<FlowReaderInstance>,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for SamplesReader {}

impl SamplesReader {
    pub(crate) fn new(reader: Arc<FlowReaderInstance>) -> Self {
        Self { reader }
    }

    #[deprecated(
        since = "0.2.0",
        note = "Flow reader lifetimes are now managed automatically. This method only consumes the handle and always returns `Ok(())`; the underlying reader is released when the last related handle is dropped."
    )]
    pub fn destroy(self) -> Result<()> {
        Ok(())
    }

    /// The whole FlowInfo is quite a chunk of data. Go for `get_config_info` or `get_runtime_info`
    /// if they contain what you need.
    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.reader.context, self.reader.as_ptr())
    }

    pub fn get_config_info(&self) -> Result<FlowConfigInfo> {
        get_config_info(&self.reader.context, self.reader.as_ptr())
    }

    pub fn get_runtime_info(&self) -> Result<mxl_sys::FlowRuntimeInfo> {
        get_runtime_info(&self.reader.context, self.reader.as_ptr())
    }

    pub fn get_samples(
        &self,
        index: u64,
        count: usize,
        timeout: Duration,
    ) -> Result<SamplesData<'_>> {
        let timeout_ns = timeout.as_nanos() as u64;
        let mut buffer_slice: mxl_sys::WrappedMultiBufferSlice = unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(self.reader.context.api.flow_reader_get_samples(
                self.reader.as_ptr(),
                index,
                count,
                timeout_ns,
                &mut buffer_slice,
            ))?;
        }
        Ok(SamplesData::new(buffer_slice))
    }

    pub fn get_samples_non_blocking(&self, index: u64, count: usize) -> Result<SamplesData<'_>> {
        let mut buffer_slice: mxl_sys::WrappedMultiBufferSlice = unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(
                self.reader
                    .context
                    .api
                    .flow_reader_get_samples_non_blocking(
                        self.reader.as_ptr(),
                        index,
                        count,
                        &mut buffer_slice,
                    ),
            )?;
        }
        Ok(SamplesData::new(buffer_slice))
    }
}
