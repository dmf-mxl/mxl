//! GStreamer Plugin for Media eXchange Layer (MXL)
//!
//! This crate implements a GStreamer plugin providing two elements for bridging
//! GStreamer pipelines with the MXL shared-memory media exchange system:
//!
//! - **mxlsrc**: A source element that reads media from an MXL flow and pushes
//!   buffers into a GStreamer pipeline
//! - **mxlsink**: A sink element that receives GStreamer buffers and writes them
//!   to an MXL flow
//!
//! ## MXL Overview
//! MXL (Media eXchange Layer) is an SDK for zero-copy media sharing between processes
//! using shared-memory ring buffers. It provides efficient, low-latency transfer of
//! video and audio data without requiring serialization or memory copies.
//!
//! ## Supported Media Formats
//! - **Video**: v210 (10-bit uncompressed YUV 4:2:2)
//! - **Audio**: F32LE (32-bit floating-point, interleaved)
//!
//! ## GStreamer Concepts (for non-GStreamer developers)
//! - **Element**: A processing unit in a pipeline (source, filter, or sink)
//! - **Plugin**: A dynamically loadable library containing one or more elements
//! - **Registration**: Making elements available to the GStreamer framework
//! - **Caps (Capabilities)**: Media format descriptions (resolution, framerate, etc.)

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: MIT OR Apache-2.0

// Allow non-Send fields in Send types (required for GStreamer's threading model)
// and unused doc comments (sometimes present in macro-generated code)
#![allow(clippy::non_send_fields_in_send_ty, unused_doc_comments)]

use gst::glib;
use gstreamer as gst;

/// MXL sink element (receives GStreamer buffers, writes to MXL flow)
mod mxlsink;

/// MXL source element (reads from MXL flow, produces GStreamer buffers)
pub mod mxlsrc;

/// Initializes the plugin by registering all elements with GStreamer.
///
/// This function is called once when the plugin is loaded. It registers both
/// the mxlsrc and mxlsink elements, making them available for use in pipelines.
///
/// # Arguments
/// * `plugin` - The GStreamer plugin instance being initialized
///
/// # Returns
/// * `Ok(())` if both elements registered successfully
/// * `Err(BoolError)` if registration failed
///
/// # GStreamer Context
/// Registration associates element names (like "mxlsrc") with their implementation
/// types, allowing gst_element_factory_make("mxlsrc") to create instances.
fn plugin_init(plugin: &gst::Plugin) -> Result<(), glib::BoolError> {
    // Register source element (reads from MXL)
    mxlsrc::register(plugin)?;

    // Register sink element (writes to MXL)
    mxlsink::register(plugin)?;

    Ok(())
}

/// GStreamer plugin metadata and entry point.
///
/// This macro generates the C-compatible plugin entry point that GStreamer
/// calls when loading the shared library. It defines:
/// - Plugin name: "mxl"
/// - Description: From CARGO_PKG_DESCRIPTION
/// - Version: From CARGO_PKG_VERSION + git commit ID
/// - License: Apache-2.0
/// - Initialization function: plugin_init
gst::plugin_define!(
    mxl,
    env!("CARGO_PKG_DESCRIPTION"),
    plugin_init,
    concat!(env!("CARGO_PKG_VERSION"), "-", env!("COMMIT_ID")),
    "Apache-2.0",
    env!("CARGO_PKG_NAME"),
    env!("CARGO_PKG_NAME"),
    env!("CARGO_PKG_REPOSITORY"),
    env!("BUILD_REL_DATE")
);
