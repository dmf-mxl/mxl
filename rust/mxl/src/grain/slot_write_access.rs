// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use tracing::error;

use crate::{Error, Result, writer::FlowWriterResource};

/// RAII grain writing session for a grain addressed by its slot.
///
/// The payload is written into the storage of the slot, which `GrainWriter::map_slots` describes,
/// by whatever means the storage type needs, for example device commands. That work must be
/// complete before `commit`. The grain is cancelled if it is dropped without being committed.
pub struct GrainSlotWriteAccess<'a> {
    writer: &'a FlowWriterResource,
    grain_info: mxl_sys::GrainInfo,
    slot: u32,
    /// Serves as a flag to know whether to cancel the grain on drop.
    committed_or_canceled: bool,
}

impl<'a> GrainSlotWriteAccess<'a> {
    pub(crate) fn new(
        writer: &'a FlowWriterResource,
        grain_info: mxl_sys::GrainInfo,
        slot: u32,
    ) -> Self {
        Self {
            writer,
            grain_info,
            slot,
            committed_or_canceled: false,
        }
    }

    /// The slot that holds the grain, an index into `MappedSlots`.
    pub fn slot(&self) -> u32 {
        self.slot
    }

    pub fn index(&self) -> u64 {
        self.grain_info.index
    }

    /// Size in bytes of the payload of a complete grain.
    pub fn grain_size(&self) -> u32 {
        self.grain_info.grainSize
    }

    pub fn total_slices(&self) -> u16 {
        self.grain_info.totalSlices
    }

    pub fn valid_slices(&self) -> u16 {
        self.grain_info.validSlices
    }

    pub fn flags(&self) -> u32 {
        self.grain_info.flags
    }

    pub fn set_flags(&mut self, flags: u32) {
        self.grain_info.flags = flags;
    }

    /// Publishes the grain with `valid_slices` valid slices and wakes the readers.
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
                self.writer
                    .context
                    .api
                    .flow_writer_commit_grain(self.writer.as_ptr(), &self.grain_info),
            )
        }
    }

    /// Discards the grain. Whatever was already written to the slot's storage stays there.
    pub fn cancel(mut self) -> Result<()> {
        self.committed_or_canceled = true;

        unsafe {
            Error::from_status(
                self.writer
                    .context
                    .api
                    .flow_writer_cancel_grain(self.writer.as_ptr()),
            )
        }
    }
}

impl<'a> Drop for GrainSlotWriteAccess<'a> {
    fn drop(&mut self) {
        if !self.committed_or_canceled
            && let Err(error) = unsafe {
                Error::from_status(
                    self.writer
                        .context
                        .api
                        .flow_writer_cancel_grain(self.writer.as_ptr()),
                )
            }
        {
            error!("Failed to cancel grain write on drop: {:?}", error);
        }
    }
}
