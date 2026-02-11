// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Grain reader implementation for discrete media flows.

use std::{sync::Arc, time::Duration};

use crate::{
    Error, FlowConfigInfo, GrainData, Result,
    flow::{
        FlowInfo,
        reader::{get_config_info, get_flow_info, get_runtime_info},
    },
    instance::InstanceContext,
};

/// Reader for discrete media grains (video frames, data packets).
///
/// Provides zero-copy access to grains stored in MXL's ring buffer. Grains
/// are accessed by index, and reads can be blocking (with timeout) or non-blocking.
///
/// # Thread Safety
///
/// `GrainReader` is `Send` but not `Sync`. Each reader should be used by only
/// one thread at a time, but can be transferred between threads.
///
/// # Examples
///
/// ```no_run
/// # use mxl::{MxlInstance, GrainReader};
/// # use std::time::Duration;
/// # fn example(instance: MxlInstance, reader: GrainReader) -> Result<(), mxl::Error> {
/// let info = reader.get_config_info()?;
/// let rate = info.common().grain_rate()?;
/// let index = instance.get_current_index(&rate);
///
/// // Blocking read with 5-second timeout
/// let grain = reader.get_complete_grain(index, Duration::from_secs(5))?;
/// println!("Read {} bytes", grain.payload.len());
/// # Ok(())
/// # }
/// ```
pub struct GrainReader {
    context: Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
}

// Safety: Readers are not thread-safe (no Sync) but can be sent between threads.
unsafe impl Send for GrainReader {}

impl GrainReader {
    /// Creates a new grain reader (internal use only).
    pub(crate) fn new(context: Arc<InstanceContext>, reader: mxl_sys::FlowReader) -> Self {
        Self { context, reader }
    }

    /// Explicitly destroys this reader, releasing resources immediately.
    ///
    /// Normally the reader is destroyed automatically when dropped.
    pub fn destroy(mut self) -> Result<()> {
        self.destroy_inner()
    }

    /// Retrieves complete flow information (config + runtime).
    ///
    /// This is a relatively heavy query. Prefer [`Self::get_config_info`] or
    /// [`Self::get_runtime_info`] if you only need specific fields.
    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.context, self.reader)
    }

    /// Retrieves flow configuration (format, rate, buffer hints).
    ///
    /// This is lighter weight than [`Self::get_info`] and returns only static
    /// configuration fields.
    pub fn get_config_info(&self) -> Result<FlowConfigInfo> {
        get_config_info(&self.context, self.reader)
    }

    /// Retrieves flow runtime state (head index, last access times).
    ///
    /// Useful for checking how much data is available before reading.
    pub fn get_runtime_info(&self) -> Result<mxl_sys::FlowRuntimeInfo> {
        get_runtime_info(&self.context, self.reader)
    }

    /// Reads a complete grain with blocking and timeout.
    ///
    /// Waits for the grain at `index` to be completely written, retrying if partial
    /// data is encountered. Returns once all slices are valid or the timeout expires.
    ///
    /// # Arguments
    ///
    /// * `index` - Grain index to read
    /// * `timeout` - Maximum time to wait for the grain to become available
    ///
    /// # Returns
    ///
    /// A zero-copy view of the grain's payload, valid for the lifetime of this reader.
    ///
    /// # Errors
    ///
    /// - [`Error::Timeout`] if the grain is not available within `timeout`
    /// - [`Error::OutOfRangeTooLate`] if the grain has been overwritten
    /// - [`Error::OutOfRangeTooEarly`] if the grain hasn't been written yet
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::GrainReader;
    /// # use std::time::Duration;
    /// # fn example(reader: GrainReader) -> Result<(), mxl::Error> {
    /// let grain = reader.get_complete_grain(100, Duration::from_secs(5))?;
    /// println!("Grain size: {} bytes", grain.payload.len());
    /// # Ok(())
    /// # }
    /// ```
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
                    self.reader,
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
        })
    }

    /// Reads a grain without blocking (may return partial data).
    ///
    /// Unlike [`Self::get_complete_grain`], this returns immediately whether or not
    /// the grain is complete. For partial grains, `payload.len() < total_size`.
    ///
    /// # Arguments
    ///
    /// * `index` - Grain index to read
    ///
    /// # Returns
    ///
    /// A zero-copy view of available grain data (may be incomplete).
    ///
    /// # Errors
    ///
    /// - [`Error::OutOfRangeTooLate`] if the grain has been overwritten
    /// - [`Error::OutOfRangeTooEarly`] if the grain hasn't been started
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::GrainReader;
    /// # fn example(reader: GrainReader) -> Result<(), mxl::Error> {
    /// let grain = reader.get_grain_non_blocking(100)?;
    /// if grain.payload.len() < grain.total_size {
    ///     println!("Partial grain: {}/{} bytes", grain.payload.len(), grain.total_size);
    /// }
    /// # Ok(())
    /// # }
    /// ```
    pub fn get_grain_non_blocking<'a>(&'a self, index: u64) -> Result<GrainData<'a>> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut payload_ptr: *mut u8 = std::ptr::null_mut();
        unsafe {
            Error::from_status(self.context.api.flow_reader_get_grain_non_blocking(
                self.reader,
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
        })
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

impl Drop for GrainReader {
    fn drop(&mut self) {
        if !self.reader.is_null()
            && let Err(err) = self.destroy_inner()
        {
            tracing::error!("Failed to release MXL flow reader (discrete): {:?}", err);
        }
    }
}
