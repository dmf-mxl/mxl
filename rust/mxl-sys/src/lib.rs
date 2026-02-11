// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! # mxl-sys: Raw FFI bindings to the MXL C library
//!
//! This crate provides low-level, unsafe Rust bindings to the MXL (Media eXchange Layer)
//! C library. It is auto-generated using `bindgen` and `libloading` for dynamic library
//! loading.
//!
//! ## Overview
//!
//! `mxl-sys` exposes:
//! - Raw C types (`Instance`, `FlowReader`, `FlowWriter`, etc.)
//! - Raw C functions (prefixed with `mxl` in C, converted to snake_case in Rust)
//! - Constants for status codes, data formats, and payload locations
//!
//! ## Usage
//!
//! **Most users should NOT use this crate directly.** Use the safe [`mxl`] wrapper crate
//! instead, which provides:
//! - Memory safety via RAII types
//! - Rust-idiomatic error handling with `Result`
//! - Strong typing for flows, grains, and samples
//!
//! This crate is only needed for:
//! - Implementing custom MXL wrappers
//! - Calling MXL functions not yet wrapped by the `mxl` crate
//!
//! ## Safety
//!
//! All functions in this crate are `unsafe` and require the caller to uphold MXL's
//! invariants:
//! - Instances must be created before any readers/writers
//! - Readers/writers are NOT thread-safe (only instances are)
//! - Pointers must remain valid for the duration of operations
//! - Null checks are the caller's responsibility
//!
//! ## Build Process
//!
//! By default, this crate builds the MXL C library from source using CMake. Set the
//! `mxl-not-built` feature to skip building and use a pre-existing library.
//!
//! [`mxl`]: https://docs.rs/mxl

// Suppress expected warnings from bindgen-generated code.
// See https://github.com/rust-lang/rust-bindgen/issues/1651.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(missing_docs)]
#![allow(rustdoc::broken_intra_doc_links)]
#![allow(rustdoc::invalid_html_tags)]
#![allow(unsafe_op_in_unsafe_fn)]
#![allow(deref_nullptr)]
#![allow(clippy::missing_safety_doc)]

extern crate libloading;

// Include bindgen-generated FFI bindings
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
