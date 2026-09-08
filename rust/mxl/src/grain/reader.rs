// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{cell::Cell, marker::PhantomData, sync::Arc, time::Duration};

use crate::{
    Error, FlowConfigInfo, GrainData, Result,
    flow::{
        FlowInfo,
        reader::{get_config_info, get_flow_info, get_runtime_info},
    },
    reader::{FlowReaderResource, FlowReaderResourceKeepAlive},
};

pub struct GrainReader {
    reader: Arc<FlowReaderResource>,
    _not_sync: PhantomData<Cell<()>>, // Prevent Sync implementation, as the underlying MXL reader
                                      // is not thread-safe.
}

impl GrainReader {
    pub(crate) fn new(reader: Arc<FlowReaderResource>) -> Self {
        Self {
            reader,
            _not_sync: PhantomData,
        }
    }

    #[allow(dead_code)]
    pub(crate) fn keep_alive(&self) -> FlowReaderResourceKeepAlive {
        self.reader.keep_alive()
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
        get_flow_info(&self.reader.context, unsafe { self.reader.as_ptr() })
    }

    pub fn get_config_info(&self) -> Result<FlowConfigInfo> {
        get_config_info(&self.reader.context, unsafe { self.reader.as_ptr() })
    }

    pub fn get_runtime_info(&self) -> Result<mxl_sys::FlowRuntimeInfo> {
        get_runtime_info(&self.reader.context, unsafe { self.reader.as_ptr() })
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
                Error::from_status(self.reader.context.api.flow_reader_get_grain(
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
            Error::from_status(self.reader.context.api.flow_reader_get_grain_non_blocking(
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
