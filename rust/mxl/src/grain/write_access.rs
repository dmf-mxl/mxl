use std::{marker::PhantomData, sync::Arc};

use tracing::error;

use crate::{instance::InstanceContext, Error, Result};

/// RAII grain writing session
///
/// Automatically cancels the grain if not explicitly committed.
pub struct GrainWriteAccess<'a> {
    context: Arc<InstanceContext>,
    writer: mxl_sys::mxlFlowWriter,
    grain_info: mxl_sys::GrainInfo,
    payload_ptr: *mut u8,
    /// Serves as a flag to know whether to cancel the grain on drop.
    committed_or_canceled: bool,
    phantom: PhantomData<&'a ()>,
}

impl<'a> GrainWriteAccess<'a> {
    pub(crate) fn new(
        context: Arc<InstanceContext>,
        writer: mxl_sys::mxlFlowWriter,
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

    pub fn payload_mut(&mut self) -> &mut [u8] {
        unsafe {
            std::slice::from_raw_parts_mut(self.payload_ptr, self.grain_info.grainSize as usize)
        }
    }

    pub fn user_data_mut(&mut self) -> &mut [u8] {
        &mut self.grain_info.userData
    }

    pub fn max_size(&self) -> u32 {
        self.grain_info.grainSize
    }

    pub fn committed_size(&self) -> u32 {
        self.grain_info.commitedSize
    }

    pub fn commit(mut self, commited_size: u32) -> Result<()> {
        self.committed_or_canceled = true;

        if commited_size > self.grain_info.grainSize {
            return Err(Error::Other(format!(
                "Commited size {} cannot exceed grain size {}.",
                commited_size, self.grain_info.grainSize
            )));
        }
        self.grain_info.commitedSize = commited_size;

        unsafe {
            Error::from_status(
                self.context
                    .api
                    .mxl_flow_writer_commit_grain(self.writer, &self.grain_info),
            )
        }
    }

    /// Please note that the behavior of canceling a grain writing is dependent on the behavior
    /// implemented in MXL itself. Particularly, if grain data has been mutated and then writing
    /// canceled, mutation will most likely stay in place, only head won't be updated, and readers
    /// notified.
    pub fn cancel(mut self) -> Result<()> {
        self.committed_or_canceled = true;

        unsafe { Error::from_status(self.context.api.mxl_flow_writer_cancel_grain(self.writer)) }
    }
}

impl<'a> Drop for GrainWriteAccess<'a> {
    fn drop(&mut self) {
        if !self.committed_or_canceled {
            if let Err(error) = unsafe {
                Error::from_status(self.context.api.mxl_flow_writer_cancel_grain(self.writer))
            } {
                error!("Failed to cancel grain write on drop: {:?}", error);
            }
        }
    }
}
