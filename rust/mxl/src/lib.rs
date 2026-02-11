// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! # MXL - Media eXchange Layer
//!
//! Safe, idiomatic Rust bindings for the MXL C library, providing high-performance
//! zero-copy shared-memory media exchange for video, audio, and data streams.
//!
//! ## Overview
//!
//! MXL enables inter-process media exchange using memory-mapped ring buffers on tmpfs.
//! This crate wraps the raw C FFI ([`mxl_sys`]) with safe Rust abstractions and RAII
//! resource management.
//!
//! ### Key Concepts
//!
//! - **Domain**: A tmpfs directory containing shared memory for media flows
//! - **Instance**: A connection to an MXL domain ([`MxlInstance`])
//! - **Flow**: A unidirectional ring buffer for media data, either discrete or continuous
//! - **Grain**: A discrete unit of media (video frame, metadata packet) accessed via [`GrainReader`]/[`GrainWriter`]
//! - **Samples**: Continuous media data (audio) accessed via [`SamplesReader`]/[`SamplesWriter`]
//!
//! ### Flow Types
//!
//! MXL supports two media flow patterns:
//!
//! - **Discrete (grain-based)**: Video frames and data packets delivered as complete units
//! - **Continuous (sample-based)**: Audio streams delivered as interleaved channel samples
//!
//! ## Architecture
//!
//! ```text
//! ┌─────────────┐
//! │ MxlInstance │  (bound to a domain)
//! └──────┬──────┘
//!        │
//!        ├─► FlowWriter ──► GrainWriter   (video/data)
//!        │              └─► SamplesWriter  (audio)
//!        │
//!        └─► FlowReader ──► GrainReader   (video/data)
//!                       └─► SamplesReader  (audio)
//! ```
//!
//! ## Examples
//!
//! ### Creating an MXL instance and writing video grains
//!
//! ```no_run
//! use mxl::{load_api, MxlInstance};
//! use std::time::Duration;
//!
//! # fn main() -> Result<(), mxl::Error> {
//! // Load the MXL dynamic library
//! let api = load_api("libmxl.so")?;
//!
//! // Create an instance bound to a tmpfs domain
//! let instance = MxlInstance::new(api, "/dev/shm/my_domain", "")?;
//!
//! // Create a flow writer from a JSON flow definition
//! let flow_def = r#"{"id": "...", "format": "urn:x-nmos:format:video", ...}"#;
//! let (writer, info, _) = instance.create_flow_writer(flow_def, None)?;
//!
//! // Convert to grain writer for discrete video data
//! let grain_writer = writer.to_grain_writer()?;
//!
//! // Write a grain at the current index
//! let rate = info.common().grain_rate()?;
//! let index = instance.get_current_index(&rate);
//! let mut access = grain_writer.open_grain(index)?;
//! access.payload_mut().fill(42); // Fill with test data
//! access.commit(access.total_slices())?; // Commit all slices
//! # Ok(())
//! # }
//! ```
//!
//! ### Reading audio samples
//!
//! ```no_run
//! use mxl::{load_api, MxlInstance};
//! use std::time::Duration;
//!
//! # fn main() -> Result<(), mxl::Error> {
//! let api = load_api("libmxl.so")?;
//! let instance = MxlInstance::new(api, "/dev/shm/my_domain", "")?;
//!
//! // Connect to an existing audio flow
//! let reader = instance.create_flow_reader("flow-uuid")?;
//! let samples_reader = reader.to_samples_reader()?;
//!
//! // Read 480 samples (10ms at 48kHz) with 5-second timeout
//! let index = samples_reader.get_runtime_info()?.headIndex;
//! let samples = samples_reader.get_samples(index, 480, Duration::from_secs(5))?;
//!
//! // Access per-channel data (may wrap at ring boundary)
//! for ch in 0..samples.num_of_channels() {
//!     let (fragment1, fragment2) = samples.channel_data(ch)?;
//!     println!("Channel {}: {} + {} bytes", ch, fragment1.len(), fragment2.len());
//! }
//! # Ok(())
//! # }
//! ```
//!
//! ## Timing and Synchronization
//!
//! MXL uses TAI timestamps (nanoseconds since SMPTE ST 2059 epoch, 1970-01-01 00:00:00 TAI):
//!
//! - [`MxlInstance::get_time`] returns current TAI time
//! - [`MxlInstance::index_to_timestamp`] / [`MxlInstance::timestamp_to_index`] convert between indices and timestamps
//! - [`MxlInstance::sleep_for`] sleeps using MXL's high-precision timing
//!
//! ## Thread Safety
//!
//! - [`MxlInstance`] is `Send + Sync` and can be shared across threads
//! - Readers and writers are `Send` but not `Sync` (not thread-safe per the MXL C API)
//! - Each thread should have its own reader/writer instances
//!
//! ## Feature Flags
//!
//! - `mxl-not-built`: Use a pre-built MXL library instead of building from source

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
