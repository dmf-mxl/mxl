// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{sync::Arc, time::Duration};

use crate::{
    Error, Result, SamplesData,
    flow::{
        FlowConfigInfo, FlowInfo,
        reader::{get_config_info, get_flow_info, get_runtime_info},
    },
    instance::InstanceContext,
    reader::FlowReaderInstance,
};

pub struct SamplesReader {
    context: Arc<InstanceContext>,
    reader: Arc<FlowReaderInstance>,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for SamplesReader {}

impl SamplesReader {
    pub(crate) fn new(context: Arc<InstanceContext>, reader: Arc<FlowReaderInstance>) -> Self {
        Self { context, reader }
    }

    #[deprecated(
        since = "0.2.0",
        note = "The MXL FlowWriter lifetime is now automatically managed internally. You should not be calling destroy() on it anymore. This function is a no-op and will be removed in a future version."
    )]
    pub fn destroy(self) -> Result<()> {
        Ok(())
    }

    /// The whole FlowInfo is quite a chunk of data. Go for `get_config_info` or `get_runtime_info`
    /// if they contain what you need.
    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.context, self.reader.as_ptr())
    }

    pub fn get_config_info(&self) -> Result<FlowConfigInfo> {
        get_config_info(&self.context, self.reader.as_ptr())
    }

    pub fn get_runtime_info(&self) -> Result<mxl_sys::FlowRuntimeInfo> {
        get_runtime_info(&self.context, self.reader.as_ptr())
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
            Error::from_status(self.context.api.flow_reader_get_samples(
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
            Error::from_status(self.context.api.flow_reader_get_samples_non_blocking(
                self.reader.as_ptr(),
                index,
                count,
                &mut buffer_slice,
            ))?;
        }
        Ok(SamplesData::new(buffer_slice))
    }
}
