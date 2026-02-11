// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Sample writer implementation for continuous media flows.

use std::sync::Arc;

use crate::{Error, Result, SamplesWriteAccess, instance::InstanceContext};

/// Writer for continuous audio sample streams.
///
/// Provides zero-copy write access to multi-channel audio buffers in MXL's ring buffer.
/// Samples are written in batches at specific indices via [`SamplesWriteAccess`] sessions.
///
/// # Thread Safety
///
/// `SamplesWriter` is `Send` but not `Sync`. Each writer should be used by only
/// one thread at a time, but can be transferred between threads.
///
/// # Examples
///
/// ```no_run
/// # use mxl::{MxlInstance, SamplesWriter};
/// # fn example(instance: MxlInstance, writer: SamplesWriter) -> Result<(), mxl::Error> {
/// let rate = mxl::Rational { numerator: 48000, denominator: 1 };
/// let index = instance.get_current_index(&rate);
///
/// // Open a batch of 480 samples (10ms at 48kHz)
/// let mut access = writer.open_samples(index, 480)?;
///
/// // Fill channels with test data
/// for ch in 0..access.channels() {
///     let (frag1, frag2) = access.channel_data_mut(ch)?;
///     frag1.fill(0x00);
///     frag2.fill(0x00);
/// }
///
/// access.commit()?;
/// # Ok(())
/// # }
/// ```
pub struct SamplesWriter {
    context: Arc<InstanceContext>,
    writer: mxl_sys::FlowWriter,
}

// Safety: Writers are not thread-safe (no Sync) but can be sent between threads.
unsafe impl Send for SamplesWriter {}

impl SamplesWriter {
    /// Creates a new samples writer (internal use only).
    pub(crate) fn new(context: Arc<InstanceContext>, writer: mxl_sys::FlowWriter) -> Self {
        Self { context, writer }
    }

    /// Explicitly destroys this writer, releasing resources immediately.
    ///
    /// Normally the writer is destroyed automatically when dropped.
    pub fn destroy(mut self) -> Result<()> {
        self.destroy_inner()
    }

    /// Opens a batch of samples for writing at the specified index.
    ///
    /// Returns a [`SamplesWriteAccess`] session that provides mutable access to
    /// multi-channel audio buffers. The session must be explicitly committed or canceled.
    ///
    /// The `index` typically points to the end of the batch (e.g., to write samples
    /// 521-1000, use `index = 1000` and `count = 480`).
    ///
    /// # Arguments
    ///
    /// * `index` - Sample index (end of the batch to write)
    /// * `count` - Number of samples to write per channel
    ///
    /// # Returns
    ///
    /// A write access session tied to this writer's lifetime.
    ///
    /// # Errors
    ///
    /// Returns an error if the MXL C API fails to open the sample buffer.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::SamplesWriter;
    /// # fn example(writer: SamplesWriter) -> Result<(), mxl::Error> {
    /// let mut access = writer.open_samples(1000, 480)?;
    ///
    /// for ch in 0..access.channels() {
    ///     let (frag1, frag2) = access.channel_data_mut(ch)?;
    ///     // Write float32 samples (interpret bytes as f32)
    ///     // let samples = bytemuck::cast_slice_mut::<u8, f32>(frag1);
    ///     // samples.fill(0.0);
    /// }
    ///
    /// access.commit()?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn open_samples<'a>(&'a self, index: u64, count: usize) -> Result<SamplesWriteAccess<'a>> {
        let mut buffer_slice: mxl_sys::MutableWrappedMultiBufferSlice =
            unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(self.context.api.flow_writer_open_samples(
                self.writer,
                index,
                count,
                &mut buffer_slice,
            ))?;
        }
        Ok(SamplesWriteAccess::new(
            self.context.clone(),
            self.writer,
            buffer_slice,
        ))
    }

    /// Internal helper to release the writer handle.
    fn destroy_inner(&mut self) -> Result<()> {
        if self.writer.is_null() {
            return Err(Error::InvalidArg);
        }

        let mut writer = std::ptr::null_mut();
        std::mem::swap(&mut self.writer, &mut writer);

        Error::from_status(unsafe {
            self.context
                .api
                .release_flow_writer(self.context.instance, writer)
        })
    }
}

impl Drop for SamplesWriter {
    /// Automatically releases the samples writer when dropped.
    fn drop(&mut self) {
        if !self.writer.is_null()
            && let Err(err) = self.destroy_inner()
        {
            tracing::error!("Failed to release MXL flow writer (continuous): {:?}", err);
        }
    }
}
