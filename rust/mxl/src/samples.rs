// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Continuous media sample handling (audio streams).
//!
//! This module provides types for reading and writing continuous media samples,
//! primarily for audio data. Samples are delivered in batches across multiple
//! channels with zero-copy access to ring buffer memory.
//!
//! # Key Types
//!
//! - [`SamplesReader`]: Reads audio samples from a flow
//! - [`SamplesWriter`]: Writes audio samples to a flow
//! - [`SamplesWriteAccess`]: RAII write session for a sample batch
//! - [`SamplesData`]: Zero-copy view of multi-channel sample data

pub mod data;
pub mod reader;
pub mod write_access;
pub mod writer;
