// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{cell::Cell, marker::PhantomData, sync::Arc, time::Duration};

use crate::{
    Error, FlowConfigInfo, GrainData, GrainSlot, GrainStorage, GrainStorageLayout, MappedSlots,
    Result,
    flow::{
        FlowInfo,
        reader::{get_config_info, get_flow_info, get_runtime_info},
    },
    reader::{FlowReaderResource, FlowReaderResourceKeepAlive},
};

pub struct GrainReader {
    reader: Arc<FlowReaderResource>,
    _not_sync: PhantomData<Cell<()>>, // Prevent Sync implementation, as the underlying MXL reader
                                      // is not thread-safe.
}

impl GrainReader {
    pub(crate) fn new(reader: Arc<FlowReaderResource>) -> Self {
        Self {
            reader,
            _not_sync: PhantomData,
        }
    }

    #[allow(dead_code)]
    pub(crate) fn keep_alive(&self) -> FlowReaderResourceKeepAlive {
        self.reader.keep_alive()
    }

    #[deprecated(
        since = "0.2.0",
        note = "Flow reader lifetimes are now managed automatically. This method only consumes the handle and always returns `Ok(())` only if no other references to the reader exists; the underlying reader is released when the last related handle is dropped."
    )]
    pub fn destroy(self) -> Result<()> {
        let _ = Arc::try_unwrap(self.reader).map_err(|_| {
            Error::Other(
                "Failed to destroy the reader. There are still other references to it.".to_string(),
            )
        })?;
        Ok(())
    }

    /// The whole FlowInfo is quite a chunk of data. Go for `get_config_info` or `get_runtime_info`
    /// if they contain what you need.
    pub fn get_info(&self) -> Result<FlowInfo> {
        get_flow_info(&self.reader.context, unsafe { self.reader.as_ptr() })
    }

    pub fn get_config_info(&self) -> Result<FlowConfigInfo> {
        get_config_info(&self.reader.context, unsafe { self.reader.as_ptr() })
    }

    pub fn get_runtime_info(&self) -> Result<mxl_sys::FlowRuntimeInfo> {
        get_runtime_info(&self.reader.context, unsafe { self.reader.as_ptr() })
    }

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
                Error::from_status(self.reader.context.api.flow_reader_get_grain(
                    self.reader.as_ptr(),
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
            index: grain_info.index,
            total_slices: grain_info.totalSlices,
        })
    }

    /// Non-blocking version of `get_complete_grain`. If the grain is not available, returns an error.
    /// If the grain is partial, it is returned as is and the payload length will be smaller than the total grain size.
    pub fn get_grain_non_blocking<'a>(&'a self, index: u64) -> Result<GrainData<'a>> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut payload_ptr: *mut u8 = std::ptr::null_mut();
        unsafe {
            Error::from_status(self.reader.context.api.flow_reader_get_grain_non_blocking(
                self.reader.as_ptr(),
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
            index: grain_info.index,
            total_slices: grain_info.totalSlices,
        })
    }

    /// The layout of the storage of the payloads. It does not change for the lifetime of the flow.
    pub fn storage_layout(&self) -> Result<GrainStorageLayout> {
        let mut layout = GrainStorageLayout::zeroed();
        unsafe {
            Error::from_status(
                self.reader
                    .context
                    .api
                    .flow_reader_get_storage_layout(self.reader.as_ptr(), &mut layout.value),
            )?;
        }
        Ok(layout)
    }

    /// Maps the storage of every slot into this process, for example by receiving the file
    /// descriptors of a backend's storage from the writer. Call it once, before reading grains with
    /// `get_grain_slot`. If a read later returns `Error::FlowInvalid`, the descriptions are stale:
    /// drop this reader and the `MappedSlots` it returned, then create a new reader and map it again.
    pub fn map_slots(&self) -> Result<MappedSlots> {
        let layout = self.storage_layout()?;
        let slot_count = layout.slot_count();
        let mut slots = vec![GrainStorage::zeroed(); slot_count as usize];
        unsafe {
            Error::from_status(self.reader.context.api.flow_reader_map_slots(
                self.reader.as_ptr(),
                slot_count,
                // GrainStorage is a transparent wrapper of mxl_sys::GrainStorage.
                slots.as_mut_ptr().cast::<mxl_sys::GrainStorage>(),
            ))?;
        }
        Ok(MappedSlots::from_reader(layout, slots, self.reader.clone()))
    }

    /// Waits up to `timeout` for the grain at `index` to have at least `min_valid_slices` valid
    /// slices, and returns the slot that holds it. `GRAIN_VALID_SLICES_ALL` waits for the complete
    /// grain. Works for every storage type: the payload is in the slot's storage, in `MappedSlots`.
    ///
    /// Returns `Error::OutOfRangeTooEarly` if the grain is not available in time,
    /// `Error::OutOfRangeTooLate` if it was overwritten, and `Error::FlowInvalid` if the flow is stale.
    pub fn get_grain_slot(
        &self,
        index: u64,
        min_valid_slices: u16,
        timeout: Duration,
    ) -> Result<GrainSlot> {
        let mut grain_info: mxl_sys::GrainInfo = unsafe { std::mem::zeroed() };
        let mut slot = 0u32;
        let timeout_ns = u64::try_from(timeout.as_nanos()).unwrap_or(u64::MAX);
        unsafe {
            Error::from_status(self.reader.context.api.flow_reader_get_grain_slot(
                self.reader.as_ptr(),
                index,
                min_valid_slices,
                timeout_ns,
                &mut grain_info,
                &mut slot,
            ))?;
        }
        Ok(GrainSlot::new(&grain_info, slot))
    }
}
