// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{cell::Cell, marker::PhantomData, sync::Arc};

use super::{slot_write_access::GrainSlotWriteAccess, write_access::GrainWriteAccess};

use crate::{
    Error, GrainStorage, GrainStorageLayout, MappedSlots, Result,
    writer::{FlowWriterResource, FlowWriterResourceKeepAlive},
};

/// MXL Flow Writer for discrete flows (grain-based data like video frames)
pub struct GrainWriter {
    writer: Arc<FlowWriterResource>,
    _not_sync: PhantomData<Cell<()>>, // Prevent Sync implementation, as the underlying MXL writer
                                      // is not thread-safe
}

impl GrainWriter {
    pub(crate) fn new(writer: Arc<FlowWriterResource>) -> Self {
        Self {
            writer,
            _not_sync: PhantomData,
        }
    }
    #[allow(dead_code)]
    pub(crate) fn keep_alive(&self) -> FlowWriterResourceKeepAlive {
        self.writer.keep_alive()
    }

    #[deprecated(
        since = "0.2.0",
        note = "Flow writer lifetimes are now managed automatically. This method only consumes the handle and always returns `Ok(())` only if no other references to the writer exists; the underlying writer is released when the last related handle is dropped."
    )]
    pub fn destroy(self) -> Result<()> {
        let _ = Arc::try_unwrap(self.writer).map_err(|_| {
            Error::Other(
                "Failed to destroy the writer. There are still other references to it.".to_string(),
            )
        })?;
        Ok(())
    }

    /// Queries the header information of a grain without opening it. Unlike `open_grain`, this
    /// does not reset the grain's valid slice count. Use it to read the state of a grain written
    /// into the flow by a Fabrics target before committing it, as opening such a grain resets its
    /// valid slices.
    pub fn get_grain_info(&self, index: u64) -> Result<mxl_sys::GrainInfo> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        unsafe {
            Error::from_status(self.writer.context.api.flow_writer_get_grain_info(
                self.writer.as_ptr(),
                index,
                &mut grain_info,
            ))?;
        }
        Ok(grain_info)
    }

    /// The current MXL implementation states a TODO to allow multiple grains to be edited at the
    /// same time. For this reason, there is no protection on the Rust level against trying to open
    /// multiple grains. If the TODO ever gets removed, it may be worth considering pattern where
    /// opening grain would consume the writer and then return it back on commit or cancel.
    pub fn open_grain<'a>(&'a self, index: u64) -> Result<GrainWriteAccess<'a>> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut payload_ptr: *mut u8 = std::ptr::null_mut();
        unsafe {
            Error::from_status(self.writer.context.api.flow_writer_open_grain(
                self.writer.as_ptr(),
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
            self.writer.as_ref(),
            grain_info,
            payload_ptr,
        ))
    }

    /// The layout of the storage of the payloads. It does not change for the lifetime of the flow.
    pub fn storage_layout(&self) -> Result<GrainStorageLayout> {
        let mut layout = GrainStorageLayout::zeroed();
        unsafe {
            Error::from_status(
                self.writer
                    .context
                    .api
                    .flow_writer_get_storage_layout(self.writer.as_ptr(), &mut layout.value),
            )?;
        }
        Ok(layout)
    }

    /// Describes the storage of every slot, so that a writer sets up its per slot resources as
    /// readers do, and finds the slot that `open_grain_slot` returns.
    pub fn map_slots(&self) -> Result<MappedSlots> {
        let layout = self.storage_layout()?;
        let slot_count = layout.slot_count();
        let mut slots = vec![GrainStorage::zeroed(); slot_count as usize];
        unsafe {
            Error::from_status(self.writer.context.api.flow_writer_map_slots(
                self.writer.as_ptr(),
                slot_count,
                // GrainStorage is a transparent wrapper of mxl_sys::GrainStorage.
                slots.as_mut_ptr().cast::<mxl_sys::GrainStorage>(),
            ))?;
        }
        Ok(MappedSlots::from_writer(layout, slots, self.writer.clone()))
    }

    /// Opens the grain at `index` and returns the slot that holds it. Works for every storage type:
    /// the application writes the payload into the slot's storage, then commits. As for
    /// `open_grain`, one grain can be open at a time.
    pub fn open_grain_slot<'a>(&'a self, index: u64) -> Result<GrainSlotWriteAccess<'a>> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut slot = 0u32;
        unsafe {
            Error::from_status(self.writer.context.api.flow_writer_open_grain_slot(
                self.writer.as_ptr(),
                index,
                &mut grain_info,
                &mut slot,
            ))?;
        }
        Ok(GrainSlotWriteAccess::new(
            self.writer.as_ref(),
            grain_info,
            slot,
        ))
    }
}
