// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{marker::PhantomData, sync::Arc};

use tracing::error;

use crate::{Error, instance::InstanceContext};

/// RAII samples writing session
///
/// Automatically cancels the samples if not explicitly committed.
///
/// The data may be split into 2 different buffer slices in case of a wrapped ring. Provides access
/// either directly to the slices or to individual samples by index inside the batch.
pub struct SamplesWriteAccess<'a> {
    context: Arc<InstanceContext>,
    writer: mxl_sys::mxlFlowWriter,
    buffer_slice: mxl_sys::mxlMutableWrappedMultiBufferSlice,
    /// Serves as a flag to know whether to cancel the samples on drop.
    committed_or_canceled: bool,
    phantom: PhantomData<&'a ()>,
}

impl<'a> SamplesWriteAccess<'a> {
    pub(crate) fn new(
        context: Arc<InstanceContext>,
        writer: mxl_sys::mxlFlowWriter,
        buffer_slice: mxl_sys::mxlMutableWrappedMultiBufferSlice,
    ) -> Self {
        Self {
            context,
            writer,
            buffer_slice,
            committed_or_canceled: false,
            phantom: PhantomData,
        }
    }

    pub fn commit(mut self) -> crate::Result<()> {
        self.committed_or_canceled = true;

        unsafe { Error::from_status(self.context.api.flow_writer_commit_samples(self.writer)) }
    }

    /// Please note that the behavior of canceling samples writing is dependent on the behavior
    /// implemented in MXL itself. Particularly, if samples data have been mutated and then writing
    /// canceled, mutation will most likely stay in place, only head won't be updated, and readers
    /// notified.
    pub fn cancel(mut self) -> crate::Result<()> {
        self.committed_or_canceled = true;

        unsafe { Error::from_status(self.context.api.flow_writer_cancel_samples(self.writer)) }
    }

    pub fn channels(&self) -> usize {
        self.buffer_slice.count
    }

    /// Provides direct access to buffer of the given channel. The access is split into two slices
    /// to cover cases when the ring is not continuous.
    ///
    /// Currently, we provide just raw bytes access. Probably we should provide some sample-based
    /// access and some index-based access (where we hide the complexity of 2 slices) as well?
    ///
    /// Samples are f32?
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
