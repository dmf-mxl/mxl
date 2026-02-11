// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! RAII sample write access for safe zero-copy audio writing.

use std::{marker::PhantomData, sync::Arc};

use tracing::error;

use crate::{Error, instance::InstanceContext};

/// RAII-protected audio sample writing session.
///
/// Provides mutable access to multi-channel audio buffers for zero-copy writing.
/// The samples are automatically canceled if not explicitly committed, ensuring
/// the flow remains consistent even if the operation is aborted.
///
/// Data may be split into two fragments per channel if the ring buffer wraps.
///
/// The lifetime `'a` is tied to the [`crate::SamplesWriter`] that created this session.
///
/// # Safety Guarantees
///
/// - Automatically cancels uncommitted samples on drop
/// - Prevents double-commit via move semantics
/// - Provides mutable buffer access only while the session is active
///
/// # Examples
///
/// ```no_run
/// # use mxl::SamplesWriter;
/// # fn example(writer: SamplesWriter) -> Result<(), mxl::Error> {
/// let mut access = writer.open_samples(1000, 480)?; // 10ms at 48kHz
///
/// // Fill each channel with data
/// for ch in 0..access.channels() {
///     let (frag1, frag2) = access.channel_data_mut(ch)?;
///     frag1.fill(0x42);
///     frag2.fill(0x42);
/// }
///
/// access.commit()?;
/// # Ok(())
/// # }
/// ```
pub struct SamplesWriteAccess<'a> {
    context: Arc<InstanceContext>,
    writer: mxl_sys::FlowWriter,
    buffer_slice: mxl_sys::MutableWrappedMultiBufferSlice,
    /// Tracks whether samples have been committed or canceled to prevent auto-cancel on drop.
    committed_or_canceled: bool,
    phantom: PhantomData<&'a ()>,
}

impl<'a> SamplesWriteAccess<'a> {
    /// Creates a new samples write session (internal use only).
    pub(crate) fn new(
        context: Arc<InstanceContext>,
        writer: mxl_sys::FlowWriter,
        buffer_slice: mxl_sys::MutableWrappedMultiBufferSlice,
    ) -> Self {
        Self {
            context,
            writer,
            buffer_slice,
            committed_or_canceled: false,
            phantom: PhantomData,
        }
    }

    /// Commits the samples, making them visible to readers.
    ///
    /// This consumes the write session and publishes the sample data to the ring
    /// buffer, advancing the head index.
    ///
    /// # Errors
    ///
    /// Returns an error if the MXL C API fails to commit the samples.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::samples::write_access::SamplesWriteAccess;
    /// # fn example(mut access: SamplesWriteAccess) -> Result<(), mxl::Error> {
    /// // Write data to all channels...
    /// for ch in 0..access.channels() {
    ///     let (frag1, frag2) = access.channel_data_mut(ch)?;
    ///     // Fill fragments...
    /// }
    ///
    /// access.commit()?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn commit(mut self) -> crate::Result<()> {
        self.committed_or_canceled = true;

        unsafe { Error::from_status(self.context.api.flow_writer_commit_samples(self.writer)) }
    }

    /// Cancels the sample write operation without committing.
    ///
    /// This explicitly cancels the write session and discards the samples. The flow's
    /// head index is not updated, and readers are not notified of new data.
    ///
    /// **Note**: The sample buffers may still contain the written data in shared
    /// memory (MXL does not zero them), but the head pointer won't advance, so
    /// readers won't see these samples.
    ///
    /// # Errors
    ///
    /// Returns an error if the MXL C API fails to cancel the samples.
    pub fn cancel(mut self) -> crate::Result<()> {
        self.committed_or_canceled = true;

        unsafe { Error::from_status(self.context.api.flow_writer_cancel_samples(self.writer)) }
    }

    /// Returns the number of audio channels.
    pub fn channels(&self) -> usize {
        self.buffer_slice.count
    }

    /// Returns mutable access to a specific channel's sample buffer.
    ///
    /// Each channel's data is returned as two mutable byte slices (fragments). If
    /// the ring buffer doesn't wrap, the second fragment will be empty.
    ///
    /// **Note**: The returned slices are raw bytes. For float32 audio (typical
    /// in MXL), you'll need to cast these bytes to `&mut [f32]` using unsafe code
    /// or a crate like `bytemuck`.
    ///
    /// # Arguments
    ///
    /// * `channel` - Channel index (0-based)
    ///
    /// # Returns
    ///
    /// A tuple `(fragment1, fragment2)` of mutable byte slices.
    ///
    /// # Errors
    ///
    /// Returns [`Error::InvalidArg`] if `channel >= channels()`.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::samples::write_access::SamplesWriteAccess;
    /// # fn example(mut access: SamplesWriteAccess) -> Result<(), mxl::Error> {
    /// let (frag1, frag2) = access.channel_data_mut(0)?;
    ///
    /// // Fill with test pattern
    /// for (i, byte) in frag1.iter_mut().enumerate() {
    ///     *byte = (i % 256) as u8;
    /// }
    /// # Ok(())
    /// # }
    /// ```
    pub fn channel_data_mut(&mut self, channel: usize) -> crate::Result<(&mut [u8], &mut [u8])> {
        if channel >= self.buffer_slice.count {
            return Err(Error::InvalidArg);
        }
        unsafe {
            let ptr_1 = (self.buffer_slice.base.fragments[0].pointer as *mut u8)
                .add(self.buffer_slice.stride * channel);
            let size_1 = self.buffer_slice.base.fragments[0].size;
            let ptr_2 = (self.buffer_slice.base.fragments[1].pointer as *mut u8)
                .add(self.buffer_slice.stride * channel);
            let size_2 = self.buffer_slice.base.fragments[1].size;
            Ok((
                std::slice::from_raw_parts_mut(ptr_1, size_1),
                std::slice::from_raw_parts_mut(ptr_2, size_2),
            ))
        }
    }
}

impl<'a> Drop for SamplesWriteAccess<'a> {
    /// Automatically cancels uncommitted samples on drop.
    ///
    /// This ensures that if a write session is abandoned (e.g., due to panic or
    /// early return), the samples are canceled rather than leaving the flow in an
    /// inconsistent state.
    fn drop(&mut self) {
        if !self.committed_or_canceled
            && let Err(error) = unsafe {
                Error::from_status(self.context.api.flow_writer_cancel_samples(self.writer))
            }
        {
            error!("Failed to cancel grain write on drop: {:?}", error);
        }
    }
}
