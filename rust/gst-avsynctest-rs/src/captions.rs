// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Phase-locked CEA-708 captions for `avsyncvideotestsrc`.
//!
//! The frame whose interval contains a pip instant carries a pop-on caption,
//! alternating "TICK" (odd pip) and "TOCK" (even pip); every other frame carries
//! a null CDP. Which frame is the caption frame is derived from the frame's PTS
//! and the pip interval `P`, so it stays locked to the pips even at fractional
//! frame rates (e.g. `30000/1001`) with no accumulated drift.
//!
//! Output is a CEA-708 CDP, the payload of an SMPTE 291 ancillary packet
//! (DID 0x61 / SDID 0x01), built with `cdp-types`/`cea708-types` — the same
//! crates GStreamer's closed-caption elements use.

use cdp_types::{CDPWriter, Framerate};
use cea708_types::tables::{Anchor, Code, DefineWindowArgs, SetPenLocationArgs, WindowBits};
use cea708_types::{DTVCCPacket, Service};

use gstreamer as gst;

/// SMPTE 334 CEA-708 CDP ancillary identifier (DID 0x61 / SDID 0x01).
pub const CC_DID: u8 = 0x61;
pub const CC_SDID: u8 = 0x01;
/// Ancillary line for the caption packet (distinct from the frame-index line).
pub const CC_LINE: u16 = 10;
/// The single CEA-708 service the captions are written to.
pub const CC_SERVICE_NO: u8 = 1;

pub const CC_TICK: &str = "TICK";
pub const CC_TOCK: &str = "TOCK";

/// The CDP framerate for a GStreamer framerate, or `None` if it has no CDP
/// identifier (CDP only encodes the eight broadcast rates).
pub fn cdp_framerate(fps: gst::Fraction) -> Option<Framerate> {
    let id = match (fps.numer(), fps.denom()) {
        (24000, 1001) => 0x1,
        (24, 1) => 0x2,
        (25, 1) => 0x3,
        (30000, 1001) => 0x4,
        (30, 1) => 0x5,
        (50, 1) => 0x6,
        (60000, 1001) => 0x7,
        (60, 1) => 0x8,
        _ => return None,
    };
    Framerate::from_id(id)
}

/// The caption for the frame spanning `[pts, next_pts)`: `TICK`/`TOCK` if a pip
/// instant `k * P` (`k >= 1`) falls in that span, else `None`. Purely a function
/// of the frame's running time, so it never drifts from the pips.
pub fn caption_for(
    pts: gst::ClockTime,
    next_pts: gst::ClockTime,
    pip_interval: gst::ClockTime,
) -> Option<&'static str> {
    let p = pip_interval.nseconds();
    let k = pts.nseconds().div_ceil(p); // smallest k with k*P >= pts
    (k >= 1 && k * p < next_pts.nseconds()).then_some(if k % 2 == 1 { CC_TICK } else { CC_TOCK })
}

/// A stateful CDP writer: one CDP per frame, so the CEA-708 sequence numbering
/// stays continuous across the stream (as a real caption service does).
#[derive(Default)]
pub struct CaptionWriter {
    cdp: CDPWriter,
    dtvcc_seq: u8,
}

impl CaptionWriter {
    /// Build the next frame's CDP. `text` present -> a pop-on caption; `None` ->
    /// a null CDP (padding only). `sequence_count` is the CDP header counter.
    pub fn next_cdp(
        &mut self,
        framerate: Framerate,
        sequence_count: u16,
        text: Option<&str>,
    ) -> Vec<u8> {
        if let Some(text) = text {
            let mut packet = DTVCCPacket::new(self.dtvcc_seq & 0x3);
            let mut service = Service::new(CC_SERVICE_NO);
            for code in caption_codes(text) {
                let _ = service.push_code(&code);
            }
            let _ = packet.push_service(service);
            self.cdp.push_packet(packet);
            self.dtvcc_seq = self.dtvcc_seq.wrapping_add(1);
        }
        self.cdp.set_sequence_count(sequence_count);
        let mut out = Vec::new();
        let _ = self.cdp.write(framerate, &mut out);
        out
    }
}

/// Pop-on caption codes: clear the windows, define + show a one-row window,
/// reset the pen, then the text (clear + pen reset + text, as ANSI/CTA-708-E
/// pop-on captioning does).
fn caption_codes(text: &str) -> Vec<Code> {
    let mut codes = vec![
        Code::DeleteWindows(!WindowBits::NONE),
        Code::DefineWindow(DefineWindowArgs::new(
            0,
            0,
            Anchor::BottomLeft,
            true,
            100,
            0,
            0,
            31,
            true,
            true,
            true,
            1,
            1,
        )),
        Code::SetPenLocation(SetPenLocationArgs::new(0, 0)),
    ];
    codes.extend(text.chars().filter_map(Code::from_char));
    codes.push(Code::ETX);
    codes
}
