// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Sample reader implementation for continuous media flows.

use std::{sync::Arc, time::Duration};

use crate::{
    Error, Result, SamplesData,
    flow::{
        FlowConfigInfo, FlowInfo,
        reader::{get_config_info, get_flow_info, get_runtime_info},
    },
    instance::InstanceContext,
};

/// Reader for continuous audio sample streams.
///
/// Provides zero-copy access to multi-channel audio samples stored in MXL's
/// ring buffer. Samples are read in batches at specific indices with support
/// for blocking and non-blocking operations.
///
/// # Thread Safety
///
/// `SamplesReader` is `Send` but not `Sync`. Each reader should be used by only
/// one thread at a time, but can be transferred between threads.
///
/// # Examples
///
/// ```no_run
/// # use mxl::{MxlInstance, SamplesReader};
/// # use std::time::Duration;
/// # fn example(instance: MxlInstance, reader: SamplesReader) -> Result<(), mxl::Error> {
/// let info = reader.get_runtime_info()?;
/// let head_index = info.headIndex;
///
/// // Read 480 samples (10ms at 48kHz)
/// let samples = reader.get_samples(head_index, 480, Duration::from_secs(1))?;
/// println!("Read {} channels", samples.num_of_channels());
/// # Ok(())
/// # }
/// ```
pub struct SamplesReader {
    context: Arc<InstanceContext>,
    reader: mxl_sys::FlowReader,
}

// Safety: Readers are not thread-safe (no Sync) but can be sent between threads.
unsafe impl Send for SamplesReader {}

impl SamplesReader {
    /// Creates a new samples reader (internal use only).
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

    /// Retrieves flow configuration (format, rate, channel count).
    ///
    /// This is lighter weight than [`Self::get_info`] and returns only static
    /// configuration fields.
    pub fn get_config_info(&self) -> Result<FlowConfigInfo> {
        get_config_info(&self.context, self.reader)
    }

    /// Retrieves flow runtime state (head index, last access times).
    ///
    /// Useful for tracking how much data is available before reading.
    pub fn get_runtime_info(&self) -> Result<mxl_sys::FlowRuntimeInfo> {
        get_runtime_info(&self.context, self.reader)
    }

    /// Reads audio samples with blocking and timeout.
    ///
    /// Waits for the requested samples to be available at the specified index.
    /// The `index` typically points to the end of the batch (e.g., for 480 samples
    /// at index 1000, samples 521-1000 are read).
    ///
    /// # Arguments
    ///
    /// * `index` - Sample index (end of the batch to read)
    /// * `count` - Number of samples to read
    /// * `timeout` - Maximum time to wait for samples to become available
    ///
    /// # Returns
    ///
    /// Zero-copy view of multi-channel sample data.
    ///
    /// # Errors
    ///
    /// - [`Error::Timeout`] if samples are not available within `timeout`
    /// - [`Error::OutOfRangeTooLate`] if samples have been overwritten
    /// - [`Error::OutOfRangeTooEarly`] if samples haven't been written yet
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::SamplesReader;
    /// # use std::time::Duration;
    /// # fn example(reader: SamplesReader) -> Result<(), mxl::Error> {
    /// // Read 480 samples at index 1000 (10ms batch at 48kHz)
    /// let samples = reader.get_samples(1000, 480, Duration::from_secs(1))?;
    ///
    /// for ch in 0..samples.num_of_channels() {
    ///     let (frag1, frag2) = samples.channel_data(ch)?;
    ///     println!("Channel {}: {} + {} bytes", ch, frag1.len(), frag2.len());
    /// }
    /// # Ok(())
    /// # }
    /// ```
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
                self.reader,
                index,
                count,
                timeout_ns,
                &mut buffer_slice,
            ))?;
        }
        Ok(SamplesData::new(buffer_slice))
    }

    /// Reads audio samples without blocking.
    ///
    /// Returns immediately with available sample data. If samples aren't ready yet,
    /// returns an error instead of waiting.
    ///
    /// # Arguments
    ///
    /// * `index` - Sample index (end of the batch to read)
    /// * `count` - Number of samples to read
    ///
    /// # Returns
    ///
    /// Zero-copy view of multi-channel sample data.
    ///
    /// # Errors
    ///
    /// - [`Error::OutOfRangeTooLate`] if samples have been overwritten
    /// - [`Error::OutOfRangeTooEarly`] if samples haven't been written yet
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::SamplesReader;
    /// # fn example(reader: SamplesReader) -> Result<(), mxl::Error> {
    /// match reader.get_samples_non_blocking(1000, 480) {
    ///     Ok(samples) => println!("Got {} channels", samples.num_of_channels()),
    ///     Err(e) => println!("Samples not ready: {:?}", e),
    /// }
    /// # Ok(())
    /// # }
    /// ```
    pub fn get_samples_non_blocking(&self, index: u64, count: usize) -> Result<SamplesData<'_>> {
        let mut buffer_slice: mxl_sys::WrappedMultiBufferSlice = unsafe { std::mem::zeroed() };
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

    /// Internal helper to release the reader handle.
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
    /// Automatically releases the samples reader when dropped.
    fn drop(&mut self) {
        if !self.reader.is_null()
            && let Err(err) = self.destroy_inner()
        {
            tracing::error!("Failed to release MXL flow reader (continuous): {:?}", err);
        }
    }
}
