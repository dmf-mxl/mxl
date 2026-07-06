// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

//! GStreamer test sources that emit a *phase-locked* audio/video pair for
//! end-to-end synchronisation testing.
//!
//! Both elements derive their content purely from each buffer's running time and
//! a shared pip interval `P` (default 1 s), so when they run in one pipeline they
//! are aligned by construction and any A/V skew observed downstream was
//! introduced by the pipeline under test.
//!
//! - [`avsyncvideotestsrc`](videosrc): a white vertical bar on black that is at
//!   screen centre exactly at every pip instant (`running_time` a multiple of
//!   `P`) and sweeps across in one `P`. Emits `video/x-raw` (v210 or UYVP) with a
//!   `GstAncillaryMeta` carrying the frame index.
//! - [`avsyncaudiotestsrc`](audiosrc): silence except a short tone pip centred on
//!   each pip instant. Emits `audio/x-raw` (F32LE, S24BE or S16BE).
//!
//! Eyeball/ear demo (bar crosses centre exactly on the beep):
//!
//! ```sh
//! gst-launch-1.0 \
//!   avsyncvideotestsrc is-live=true ! videoconvert ! autovideosink \
//!   avsyncaudiotestsrc is-live=true ! audioconvert ! autoaudiosink
//! ```

use gst::glib;
use gstreamer as gst;

pub mod audiosrc;
pub mod signal;
pub mod videosrc;

fn plugin_init(plugin: &gst::Plugin) -> Result<(), glib::BoolError> {
    videosrc::register(plugin)?;
    audiosrc::register(plugin)?;
    Ok(())
}

gst::plugin_define!(
    avsynctest,
    env!("CARGO_PKG_DESCRIPTION"),
    plugin_init,
    concat!(env!("CARGO_PKG_VERSION"), "-", env!("COMMIT_ID")),
    "Apache-2.0",
    env!("CARGO_PKG_NAME"),
    env!("CARGO_PKG_NAME"),
    env!("CARGO_PKG_REPOSITORY"),
    env!("BUILD_REL_DATE")
);
