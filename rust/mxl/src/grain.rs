// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Discrete media grain handling (video frames and data packets).
//!
//! This module provides types for reading and writing discrete units of media
//! called "grains". Grains are complete, atomic units like video frames or
//! data packets in ST 291 format.
//!
//! # Key Types
//!
//! - [`GrainReader`]: Reads grains from a flow (zero-copy via shared memory)
//! - [`GrainWriter`]: Writes grains to a flow
//! - [`GrainWriteAccess`]: RAII write session for a single grain
//! - [`GrainData`]: Zero-copy view of grain payload data

pub mod data;
pub mod reader;
pub mod write_access;
pub mod writer;
