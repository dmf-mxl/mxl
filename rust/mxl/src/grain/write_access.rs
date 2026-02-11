// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! RAII grain write access for safe zero-copy writing.

use std::{marker::PhantomData, sync::Arc};

use tracing::error;

use crate::{Error, Result, instance::InstanceContext};

/// RAII-protected grain writing session.
///
/// Provides mutable access to a grain's payload buffer for zero-copy writing.
/// The grain is automatically canceled if not explicitly committed, ensuring
/// the flow remains consistent even if the operation is aborted.
///
/// The lifetime `'a` is tied to the [`crate::GrainWriter`] that created this session.
///
/// # Safety Guarantees
///
/// - Automatically cancels uncommitted grains on drop
/// - Prevents double-commit via move semantics
/// - Provides mutable payload access only while the session is active
///
/// # Examples
///
/// ```no_run
/// # use mxl::GrainWriter;
/// # fn example(writer: GrainWriter) -> Result<(), mxl::Error> {
/// let mut access = writer.open_grain(100)?;
///
/// // Fill grain with data
/// let payload = access.payload_mut();
/// payload.fill(42);
///
/// // Commit all slices
/// access.commit(access.total_slices())?;
/// # Ok(())
/// # }
/// ```
pub struct GrainWriteAccess<'a> {
    context: Arc<InstanceContext>,
    writer: mxl_sys::FlowWriter,
    grain_info: mxl_sys::GrainInfo,
    payload_ptr: *mut u8,
    /// Tracks whether the grain has been committed or canceled to prevent auto-cancel on drop.
    committed_or_canceled: bool,
    phantom: PhantomData<&'a ()>,
}

impl<'a> GrainWriteAccess<'a> {
    /// Creates a new grain write session (internal use only).
    pub(crate) fn new(
        context: Arc<InstanceContext>,
        writer: mxl_sys::FlowWriter,
        grain_info: mxl_sys::GrainInfo,
        payload_ptr: *mut u8,
    ) -> Self {
        Self {
            context,
            writer,
            grain_info,
            payload_ptr,
            committed_or_canceled: false,
            phantom: Default::default(),
        }
    }

    /// Returns mutable access to the grain's payload buffer.
    ///
    /// This provides zero-copy write access to the shared memory ring buffer.
    /// Modifications are visible to readers once the grain is committed.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::grain::write_access::GrainWriteAccess;
    /// # fn example(mut access: GrainWriteAccess) -> Result<(), mxl::Error> {
    /// let payload = access.payload_mut();
    /// for (i, byte) in payload.iter_mut().enumerate() {
    ///     *byte = (i % 256) as u8;
    /// }
    /// # Ok(())
    /// # }
    /// ```
    pub fn payload_mut(&mut self) -> &mut [u8] {
        unsafe {
            std::slice::from_raw_parts_mut(self.payload_ptr, self.grain_info.grainSize as usize)
        }
    }

    /// Returns the maximum size of the grain payload in bytes.
    pub fn max_size(&self) -> u32 {
        self.grain_info.grainSize
    }

    /// Returns the total number of slices in this grain.
    ///
    /// A grain may be divided into multiple slices. This returns the total
    /// number available, which should be passed to [`Self::commit`] for a
    /// complete grain.
    pub fn total_slices(&self) -> u16 {
        self.grain_info.totalSlices
    }

    /// Commits the grain, making it visible to readers.
    ///
    /// This consumes the write session and publishes the grain data to the ring
    /// buffer. The specified number of valid slices determines how much of the
    /// grain is considered complete.
    ///
    /// # Arguments
    ///
    /// * `valid_slices` - Number of complete slices (typically [`Self::total_slices`]
    ///   for a fully written grain)
    ///
    /// # Errors
    ///
    /// Returns an error if:
    /// - `valid_slices` exceeds [`Self::total_slices`]
    /// - The MXL C API fails to commit the grain
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::grain::write_access::GrainWriteAccess;
    /// # fn example(mut access: GrainWriteAccess) -> Result<(), mxl::Error> {
    /// // Write data...
    /// access.payload_mut().fill(0xFF);
    ///
    /// // Commit all slices
    /// access.commit(access.total_slices())?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn commit(mut self, valid_slices: u16) -> Result<()> {
        self.committed_or_canceled = true;

        if valid_slices > self.grain_info.totalSlices {
            return Err(Error::Other(format!(
                "Valid slices {} cannot exceed total slices {}.",
                valid_slices, self.grain_info.totalSlices
            )));
        }
        self.grain_info.validSlices = valid_slices;

        unsafe {
            Error::from_status(
                self.context
                    .api
                    .flow_writer_commit_grain(self.writer, &self.grain_info),
            )
        }
    }

    /// Cancels the grain write operation without committing.
    ///
    /// This explicitly cancels the write session and discards the grain. The flow's
    /// head index is not updated, and readers are not notified of new data.
    ///
    /// **Note**: The payload buffer may still contain the written data in shared
    /// memory (MXL does not zero it), but the head pointer won't advance, so
    /// readers won't see this grain.
    ///
    /// # Errors
    ///
    /// Returns an error if the MXL C API fails to cancel the grain.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # use mxl::grain::write_access::GrainWriteAccess;
    /// # fn example(mut access: GrainWriteAccess) -> Result<(), mxl::Error> {
    /// // Write some data...
    /// access.payload_mut()[0] = 42;
    ///
    /// // Decide not to commit
    /// if should_abort() {
    ///     access.cancel()?;
    /// }
    /// # Ok(())
    /// # }
    /// # fn should_abort() -> bool { true }
    /// ```
    pub fn cancel(mut self) -> Result<()> {
        self.committed_or_canceled = true;

        unsafe { Error::from_status(self.context.api.flow_writer_cancel_grain(self.writer)) }
    }
}

impl<'a> Drop for GrainWriteAccess<'a> {
    /// Automatically cancels uncommitted grains on drop.
    ///
    /// This ensures that if a write session is abandoned (e.g., due to panic or
    /// early return), the grain is canceled rather than leaving the flow in an
    /// inconsistent state.
    fn drop(&mut self) {
        if !self.committed_or_canceled
            && let Err(error) = unsafe {
                Error::from_status(self.context.api.flow_writer_cancel_grain(self.writer))
            }
        {
            error!("Failed to cancel grain write on drop: {:?}", error);
        }
    }
}
