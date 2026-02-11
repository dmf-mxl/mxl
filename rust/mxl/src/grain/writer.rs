// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Grain writer implementation for discrete media flows.

use std::sync::Arc;

use super::write_access::GrainWriteAccess;

use crate::{Error, Result, instance::InstanceContext};

/// Writer for discrete media grains (video frames, data packets).
///
/// Provides zero-copy write access to grains in MXL's ring buffer. Each grain
/// is opened at a specific index, written via [`GrainWriteAccess`], and then
/// committed to make it visible to readers.
///
/// # Thread Safety
///
/// `GrainWriter` is `Send` but not `Sync`. Each writer should be used by only
/// one thread at a time, but can be transferred between threads.
///
/// # Examples
///
/// ```no_run
/// # use mxl::{MxlInstance, GrainWriter};
/// # fn example(instance: MxlInstance, writer: GrainWriter) -> Result<(), mxl::Error> {
/// // Open a grain for writing at the current index
/// let rate = mxl::Rational { numerator: 60, denominator: 1 };
/// let index = instance.get_current_index(&rate);
///
/// let mut access = writer.open_grain(index)?;
/// access.payload_mut().fill(0xFF); // Write data
/// access.commit(access.total_slices())?; // Commit
/// # Ok(())
/// # }
/// ```
pub struct GrainWriter {
    context: Arc<InstanceContext>,
    writer: mxl_sys::FlowWriter,
}

// Safety: Writers are not thread-safe (no Sync) but can be sent between threads.
unsafe impl Send for GrainWriter {}

impl GrainWriter {
    /// Creates a new grain writer (internal use only).
    pub(crate) fn new(context: Arc<InstanceContext>, writer: mxl_sys::FlowWriter) -> Self {
        Self { context, writer }
    }

    /// Explicitly destroys this writer, releasing resources immediately.
    ///
    /// Normally the writer is destroyed automatically when dropped.
    pub fn destroy(mut self) -> Result<()> {
        self.destroy_inner()
    }

    /// Opens a grain for writing at the specified index.
    ///
    /// Returns a [`GrainWriteAccess`] session that provides mutable access to the
    /// grain's payload buffer. The session must be explicitly committed or canceled.
    ///
    /// **Note**: The current MXL implementation may not support opening multiple
    /// grains simultaneously. If this changes, the Rust API may be updated to
    /// enforce single-grain access via move semantics.
    ///
    /// # Arguments
    ///
    /// * `index` - Grain index to write (typically the current or next index)
    ///
    /// # Returns
    ///
    /// A write access session tied to this writer's lifetime.
    ///
    /// # Errors
    ///
    /// Returns an error if the MXL C API fails to open the grain.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::GrainWriter;
    /// # fn example(writer: GrainWriter) -> Result<(), mxl::Error> {
    /// let mut access = writer.open_grain(100)?;
    /// let payload = access.payload_mut();
    ///
    /// // Fill with test pattern
    /// for (i, byte) in payload.iter_mut().enumerate() {
    ///     *byte = (i % 256) as u8;
    /// }
    ///
    /// access.commit(access.total_slices())?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn open_grain<'a>(&'a self, index: u64) -> Result<GrainWriteAccess<'a>> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut payload_ptr: *mut u8 = std::ptr::null_mut();
        unsafe {
            Error::from_status(self.context.api.flow_writer_open_grain(
                self.writer,
                index,
                &mut grain_info,
                &mut payload_ptr,
            ))?;
        }

        if payload_ptr.is_null() {
            return Err(Error::Other(format!(
                "Failed to open grain payload for index {index}.",
            )));
        }

        Ok(GrainWriteAccess::new(
            self.context.clone(),
            self.writer,
            grain_info,
            payload_ptr,
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

impl Drop for GrainWriter {
    /// Automatically releases the grain writer when dropped.
    fn drop(&mut self) {
        if !self.writer.is_null()
            && let Err(err) = self.destroy_inner()
        {
            tracing::error!("Failed to release MXL flow writer (discrete): {:?}", err);
        }
    }
}
