// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

mod api;
mod error;
mod flow;
mod grain;
mod instance;
mod samples;

pub mod config;

pub use api::{MxlApi, load_api};
pub use error::{Error, Result};
pub use flow::{reader::FlowReader, writer::FlowWriter, *};
pub use grain::{
    data::*, reader::GrainReader, write_access::GrainWriteAccess, writer::GrainWriter,
};
pub use instance::MxlInstance;
pub use mxl_sys::Rational;
pub use samples::{
    data::*, reader::SamplesReader, write_access::SamplesWriteAccess, writer::SamplesWriter,
};
