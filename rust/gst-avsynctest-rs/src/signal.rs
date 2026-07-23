// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Shared parameters and phase maths for the aligned A/V test sources.
//!
//! The two elements never share state; they agree only on the pip interval `P`
//! and these functions of running time, which is what makes their output
//! phase-locked when they run under one clock.

use gstreamer as gst;

/// Default pip interval `P`: one second between beeps / bar-centre crossings.
pub const DEFAULT_PIP_INTERVAL: gst::ClockTime = gst::ClockTime::from_seconds(1);

pub const DEFAULT_FRAMERATE_NUM: i32 = 30_000;
pub const DEFAULT_FRAMERATE_DEN: i32 = 1_001;
pub const DEFAULT_WIDTH: i32 = 1_280;
pub const DEFAULT_HEIGHT: i32 = 720;

pub const DEFAULT_RATE: i32 = 48_000;
pub const DEFAULT_CHANNELS: i32 = 1;
pub const DEFAULT_SAMPLES_PER_BUFFER: i32 = 1_024;
pub const DEFAULT_PIP_FREQ_HZ: f64 = 1_000.0;
pub const DEFAULT_PIP_VOLUME: f64 = 0.8;
/// Default pip length: a short tone burst (a handful of `pip-freq` cycles, like
/// `audiotestsrc wave=ticks`), so the pip marks its instant sharply rather than
/// spanning a large fraction of a video frame.
pub const DEFAULT_PIP_DURATION: gst::ClockTime = gst::ClockTime::from_mseconds(4);

/// SMPTE 291 ancillary identifier carrying the frame index, in the user
/// application space (DID 0x5F / SDID 0xFF); not exposed as a property.
pub const ANC_DID: u8 = 0x5F;
pub const ANC_SDID: u8 = 0xFF;
pub const ANC_LINE: u16 = 9;
pub const ANC_OFFSET: u16 = 0;

/// 10-bit narrow-range luma for black / white and neutral chroma.
pub const LUMA_BLACK: u16 = 64;
pub const LUMA_WHITE: u16 = 940;
pub const CHROMA_NEUTRAL: u16 = 512;

/// Phase within the current pip interval, in `[0, 1)`. Zero at every pip instant.
pub fn phase(running: gst::ClockTime, pip_interval: gst::ClockTime) -> f64 {
    let p = pip_interval.nseconds();
    (running.nseconds() % p) as f64 / p as f64
}

/// Column the bar centre sits at for a given phase: screen centre at `phase == 0`,
/// sweeping right and wrapping once per interval.
pub fn bar_centre_column(phase: f64, width: u32) -> u32 {
    let w = width as f64;
    ((phase * w + w / 2.0).rem_euclid(w)) as u32 % width
}

/// Circular column distance, so the bar wraps cleanly at the frame edge.
pub fn circular_distance(a: u32, b: u32, width: u32) -> u32 {
    let d = a.abs_diff(b);
    d.min(width - d)
}
