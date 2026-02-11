//! MXL Source Element
//!
//! This module implements `mxlsrc`, a GStreamer source element that reads
//! media from an MXL (Media eXchange Layer) flow and produces GStreamer
//! buffers for downstream elements.
//!
//! ## Responsibilities
//! - Connects to existing MXL flows (video or audio)
//! - Reads grains/samples from shared memory ring buffers
//! - Converts MXL data to GStreamer buffers with proper timestamps
//! - Handles synchronization and clock management
//!
//! ## Properties (set before PLAYING state)
//! - `video-flow-id`: UUID of the MXL video flow to read from
//! - `audio-flow-id`: UUID of the MXL audio flow to read from
//! - `domain`: Shared memory path (e.g., "/dev/shm")
//!
//! ## Example Pipeline
//! ```bash
//! gst-launch-1.0 mxlsrc video-flow-id=<uuid> domain=/dev/shm ! \
//!     videoconvert ! autovideosink
//! ```
//!
//! ## Architecture
//! - **PushSrc base class**: Implements a source that produces buffers on-demand
//! - **Live source**: Operates in live mode (timestamps based on running time)
//! - **Zero-copy**: Reads directly from MXL shared memory, copies to GStreamer buffers

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

/// Audio buffer creation logic (reads samples from MXL, produces buffers)
mod create_audio;

/// Video buffer creation logic (reads grains from MXL, produces buffers)
mod create_video;

/// Core implementation (properties, state management, GStreamer trait impls)
mod imp;

/// Helper functions for MXL initialization and caps negotiation
mod mxl_helper;

/// Unit and integration tests for mxlsrc
mod src_tests;

/// State structures (settings, flow readers, timing info)
mod state;

/// GLib wrapper type for the MxlSrc element.
///
/// ## Inheritance Chain (GStreamer class hierarchy)
/// - `gst::Object` (base GStreamer object)
/// - `gst::Element` (has pads, state machine)
/// - `gst_base::BaseSrc` (source-specific behavior: scheduling, live mode)
/// - `gst_base::PushSrc` (pushes buffers on-demand via create())
/// - `MxlSrc` (our custom implementation)
glib::wrapper! {
    pub struct MxlSrc(ObjectSubclass<imp::MxlSrc>) @extends gst_base::PushSrc, gst_base::BaseSrc, gst::Element, gst::Object;
}

/// Registers the mxlsrc element with GStreamer.
///
/// Called during plugin initialization to make "mxlsrc" available via
/// `gst_element_factory_make("mxlsrc")` or gst-launch.
///
/// # Arguments
/// * `plugin` - The parent plugin instance
///
/// # Returns
/// * `Ok(())` if registration succeeded
/// * `Err(BoolError)` if the element name conflicts
pub fn register(plugin: &gst::Plugin) -> Result<(), glib::BoolError> {
    gst::Element::register(
        Some(plugin),
        "mxlsrc",
        gst::Rank::NONE,  // Not auto-selected during autoplugging
        MxlSrc::static_type(),
    )
}
