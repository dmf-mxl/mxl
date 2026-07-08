// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

//! Recovery-side analysis for the aligned A/V test signal: locate the video
//! bar, the audio pips, and the phase-locked caption produced by
//! [`avsyncvideotestsrc`](crate::videosrc) and
//! [`avsyncaudiotestsrc`](crate::audiosrc). Kept transport-agnostic so any
//! pipeline under test — a bare `videoconvert`, an MXL round-trip, an NMOS
//! sender/receiver — can assert A/V (and caption) alignment on recovered
//! samples the same way.

use cdp_types::CDPParser;
use gstreamer as gst;
use gstreamer_video as gst_video;
use gstreamer_video::prelude::*;

use crate::captions;

/// Brightest-column centroid of the top row, decoded to `GRAY8`. `None` if the
/// row has no bright pixels (should not happen for the always-present bar).
pub fn bar_centroid(sample: &gst::Sample) -> Option<f64> {
    let caps = sample.caps()?;
    let info = gst_video::VideoInfo::from_caps(caps).ok()?;
    let buffer = sample.buffer()?;
    let frame = gst_video::VideoFrameRef::from_buffer_ref_readable(buffer, &info).ok()?;
    let stride = frame.plane_stride()[0] as usize;
    let row = &frame.plane_data(0).ok()?[..stride];
    let width = info.width() as usize;
    let mut sum = 0.0f64;
    let mut count = 0.0f64;
    for (x, &v) in row.iter().take(width).enumerate() {
        if v > 128 {
            sum += x as f64;
            count += 1.0;
        }
    }
    (count > 0.0).then_some(sum / count)
}

/// Reinterpret an F32LE audio buffer's bytes as native `f32` samples (the host
/// is little-endian). Panics if the buffer is not `f32`-aligned. Typically used
/// to turn a recovered audio buffer into the `(time_ns, amplitude)` pairs
/// [`detect_pips`] consumes.
pub fn f32le_samples(bytes: &[u8]) -> &[f32] {
    let (head, body, tail) = unsafe { bytes.align_to::<f32>() };
    assert!(
        head.is_empty() && tail.is_empty(),
        "misaligned F32LE buffer"
    );
    body
}

/// Running-time centre of each tone pip from `(time_ns, amplitude)` samples. The
/// envelope is binned (5 ms bins, so a 1 kHz tone's zero-crossings never split a
/// pip) and contiguous bins whose energy clears a fraction of the peak are
/// grouped; each pip's time is the *midpoint* of its group. A midpoint is
/// unbiased for the symmetric burst `avsyncaudiotestsrc` emits, whereas an
/// energy-weighted centroid is skewed by how the burst's samples fall across the
/// bin edges.
pub fn detect_pips(audio: &[(u64, f32)]) -> Vec<u64> {
    if audio.is_empty() {
        return Vec::new();
    }
    const BIN_NS: u64 = 5_000_000;
    let t0 = audio.first().unwrap().0;
    let tn = audio.last().unwrap().0;
    let nbins = ((tn - t0) / BIN_NS + 1) as usize;
    let mut energy = vec![0.0f64; nbins];
    for &(t, a) in audio {
        let b = ((t - t0) / BIN_NS) as usize;
        energy[b] += (a as f64) * (a as f64);
    }
    let peak = energy.iter().copied().fold(0.0, f64::max);
    let threshold = peak * 0.1;
    let bin_centre = |b: usize| t0 + b as u64 * BIN_NS + BIN_NS / 2;
    let mut pips = Vec::new();
    let mut i = 0;
    while i < nbins {
        if energy[i] > threshold {
            let first = i;
            while i < nbins && energy[i] > threshold {
                i += 1;
            }
            pips.push((bin_centre(first) + bin_centre(i - 1)) / 2);
        } else {
            i += 1;
        }
    }
    pips
}

/// The CEA-708 CDP the video source attaches (DID [`CC_DID`](captions::CC_DID) /
/// SDID [`CC_SDID`](captions::CC_SDID)), as raw bytes recovered from the
/// ancillary meta's 10-bit words. `None` if the frame carries no caption
/// ancillary.
pub fn caption_cdp_bytes(buffer: &gst::BufferRef) -> Option<Vec<u8>> {
    buffer
        .iter_meta::<gst_video::video_meta::AncillaryMeta>()
        .find(|m| (m.did() & 0xFF) as u8 == captions::CC_DID)
        .map(|m| m.data().iter().map(|&w| (w & 0xFF) as u8).collect())
}

/// The caption text carried by one frame's CDP (service
/// [`CC_SERVICE_NO`](captions::CC_SERVICE_NO)), or `None` for a null CDP.
pub fn decode_caption(parser: &mut CDPParser, cdp: &[u8]) -> Option<String> {
    parser.parse(cdp).expect("valid CDP");
    let packet = parser.pop_packet()?;
    let mut text = String::new();
    for service in packet.services() {
        if service.number() == captions::CC_SERVICE_NO {
            text.extend(service.codes().iter().filter_map(|code| code.char()));
        }
    }
    (!text.is_empty()).then_some(text)
}
