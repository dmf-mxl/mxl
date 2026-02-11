//! MXL Sink Element
//!
//! This module implements `mxlsink`, a GStreamer sink element that receives
//! media buffers from a pipeline and writes them to an MXL (Media eXchange Layer)
//! flow for zero-copy sharing with other processes.
//!
//! ## Responsibilities
//! - Accepts video (v210) or audio (F32LE) buffers from upstream elements
//! - Creates or attaches to MXL flows using shared memory ring buffers
//! - Synchronizes buffer presentation with the pipeline clock
//! - Converts GStreamer buffer formats to MXL grain/sample formats
//!
//! ## Properties (set before PLAYING state)
//! - `flow-id`: UUID of the target MXL flow (creates if doesn't exist)
//! - `domain`: Shared memory path (e.g., "/dev/shm")
//!
//! ## Example Pipeline
//! ```bash
//! gst-launch-1.0 videotestsrc ! video/x-raw,format=v210 ! \
//!     mxlsink flow-id=<uuid> domain=/dev/shm
//! ```

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// Copyright (C) 2020 Sebastian Dröge <sebastian@centricular.com>
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use gst::glib;
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_base as gst_base;

/// Core implementation (properties, state management, GStreamer trait impls)
mod imp;

/// Audio buffer rendering logic (converts GStreamer audio to MXL samples)
mod render_audio;

/// Video buffer rendering logic (converts GStreamer video to MXL grains)
mod render_video;

/// Unit and integration tests for mxlsink
mod sink_tests;

/// State structures (settings, flow configuration, writer handles)
mod state;

/// GLib wrapper type for the MxlSink element.
///
/// This type provides the public API for the element and implements GStreamer's
/// object hierarchy. It wraps the internal `imp::MxlSink` implementation.
///
/// ## Inheritance Chain (GStreamer class hierarchy)
/// - `gst::Object` (base GStreamer object with name, parent, etc.)
/// - `gst::Element` (has pads, state machine, clock)
/// - `gst_base::BaseSink` (sink-specific behavior: sync, preroll, etc.)
/// - `MxlSink` (our custom implementation)
glib::wrapper! {
    pub struct MxlSink(ObjectSubclass<imp::MxlSink>) @extends gst_base::PushSrc, gst_base::BaseSink, gst::Element, gst::Object;
}

/// Registers the mxlsink element with GStreamer.
///
/// Called during plugin initialization to make "mxlsink" available for use
/// in pipelines via `gst_element_factory_make("mxlsink")` or the gst-launch
/// command-line tool.
///
/// # Arguments
/// * `plugin` - The parent plugin instance
///
/// # Returns
/// * `Ok(())` if registration succeeded
/// * `Err(BoolError)` if the element name conflicts or registration fails
///
/// # Rank
/// The rank is NONE, meaning this element won't be auto-selected during
/// autoplugging. Users must explicitly request it by name.
pub fn register(plugin: &gst::Plugin) -> Result<(), glib::BoolError> {
    gst::Element::register(
        Some(plugin),
        "mxlsink",
        gst::Rank::NONE,
        MxlSink::static_type(),
    )
}
