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

    /// The whole FlowInfo is quite a chunk of data. Go for `get_config_info` or `get_runtime_info`
    /// if they contain what you need.
    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.context, self.reader)
    }

    pub fn get_config_info(&self) -> Result<FlowConfigInfo> {
        get_config_info(&self.context, self.reader)
    }

    pub fn get_runtime_info(&self) -> Result<mxl_sys::mxlFlowRuntimeInfo> {
        get_runtime_info(&self.context, self.reader)
    }

    pub fn get_samples(
        &self,
        index: u64,
        count: usize,
        timeout: Duration,
    ) -> Result<SamplesData<'_>> {
        let timeout_ns = timeout.as_nanos() as u64;
        let mut buffer_slice: mxl_sys::mxlWrappedMultiBufferSlice = unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(self.context.api.flow_reader_get_samples(
                self.reader,
                index,
                count,
                timeout_ns,
                &mut buffer_slice,
            ))?;
        }
        Ok(SamplesData::new(buffer_slice))
    }

    pub fn get_samples_non_blocking(&self, index: u64, count: usize) -> Result<SamplesData<'_>> {
        let mut buffer_slice: mxl_sys::mxlWrappedMultiBufferSlice = unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(self.context.api.flow_reader_get_samples_non_blocking(
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
                .release_flow_reader(self.context.instance, reader)
        })
    }
}

impl Drop for SamplesReader {
    fn drop(&mut self) {
        if !self.reader.is_null()
            && let Err(err) = self.destroy_inner()
        {
            tracing::error!("Failed to release MXL flow reader (continuous): {:?}", err);
        }
    }
}
