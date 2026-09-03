// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{sync::Arc, time::Duration};

use crate::{
    Error, FlowConfigInfo, GrainData, Result,
    flow::{
        FlowInfo,
        reader::{get_config_info, get_flow_info, get_runtime_info},
    },
    instance::InstanceContext,
    reader::FlowReaderInstance,
};

pub struct GrainReader {
    context: Arc<InstanceContext>,
    reader: Arc<FlowReaderInstance>,
}

/// The MXL readers and writers are not thread-safe, so we do not implement `Sync` for them, but
/// there is no reason to not implement `Send`.
unsafe impl Send for GrainReader {}

impl GrainReader {
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

    pub fn get_complete_grain<'a>(
        &'a self,
        index: u64,
        timeout: Duration,
    ) -> Result<GrainData<'a>> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut payload_ptr: *mut u8 = std::ptr::null_mut();
        let timeout_ns = timeout.as_nanos() as u64;
        loop {
            unsafe {
                Error::from_status(self.context.api.flow_reader_get_grain(
                    self.reader.as_ptr(),
                    index,
                    timeout_ns,
                    &mut grain_info,
                    &mut payload_ptr,
                ))?;
            }
            if grain_info.validSlices != grain_info.totalSlices {
                // We don't need partial grains. Wait for the grain to be complete.
                continue;
            }
            if payload_ptr.is_null() {
                return Err(Error::Other(format!(
                    "Failed to get grain payload for index {index}.",
                )));
            }
            break;
        }

        // SAFETY
        // We know that the lifetime is as long as the flow, so it is at least self's lifetime.
        // It may happen that the buffer is overwritten by a subsequent write, but it is safe.
        let payload =
            unsafe { std::slice::from_raw_parts(payload_ptr, grain_info.grainSize as usize) };

        Ok(GrainData {
            payload,
            total_size: grain_info.grainSize as usize,
            flags: grain_info.flags,
            index: grain_info.index,
        })
    }

    /// Non-blocking version of `get_complete_grain`. If the grain is not available, returns an error.
    /// If the grain is partial, it is returned as is and the payload length will be smaller than the total grain size.
    pub fn get_grain_non_blocking<'a>(&'a self, index: u64) -> Result<GrainData<'a>> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut payload_ptr: *mut u8 = std::ptr::null_mut();
        unsafe {
            Error::from_status(self.context.api.flow_reader_get_grain_non_blocking(
                self.reader.as_ptr(),
                index,
                &mut grain_info,
                &mut payload_ptr,
            ))?;
        }

        if payload_ptr.is_null() {
            return Err(Error::Other(format!(
                "Failed to get grain payload for index {index}.",
            )));
        }

        // SAFETY
        // We know that the lifetime is as long as the flow, so it is at least self's lifetime.
        // It may happen that the buffer is overwritten by a subsequent write, but it is safe.
        let payload =
            unsafe { std::slice::from_raw_parts(payload_ptr, grain_info.grainSize as usize) };

        Ok(GrainData {
            payload,
            total_size: grain_info.grainSize as usize,
            flags: grain_info.flags,
            index: grain_info.index,
        })
    }
}
