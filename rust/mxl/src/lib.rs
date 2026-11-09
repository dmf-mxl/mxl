// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

mod api;
mod error;
mod flow;
mod grain;
mod instance;
mod samples;
mod storage;

pub mod config;

pub use api::{MxlApi, load_api};
pub use error::{Error, Result};
pub use flow::{reader::FlowReader, writer::FlowWriter, *};
pub use grain::{
    data::*, reader::GrainReader, slot_write_access::GrainSlotWriteAccess,
    write_access::GrainWriteAccess, writer::GrainWriter,
};
pub use instance::MxlInstance;
pub const MXL_DATA_FORMAT_GRAIN_SIZE: usize = mxl_sys::MXL_DATA_FORMAT_GRAIN_SIZE as usize;
pub use mxl_sys::Rational;
pub use samples::{
    data::*, reader::SamplesReader, write_access::SamplesWriteAccess, writer::SamplesWriter,
};
pub use storage::{
    GRAIN_VALID_SLICES_ALL, GrainPlaneLayout, GrainSlot, GrainStorage, GrainStorageLayout,
    MappedSlots, PAYLOAD_STORAGE_HOST_POINTER,
};

#[cfg(feature = "mxl-fabrics-ofi")]
pub mod fabrics;
#[cfg(feature = "mxl-fabrics-ofi")]
pub use api::{MxlFabricsApi, load_fabrics_api};
