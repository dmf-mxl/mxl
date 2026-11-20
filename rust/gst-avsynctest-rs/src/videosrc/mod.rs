// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * SECTION:element-avsyncvideotestsrc
 * @see_also: avsyncaudiotestsrc
 *
 * `avsyncvideotestsrc` is one half of a phase-locked audio/video test pair for
 * measuring end-to-end A/V synchronisation. It renders a white vertical bar on
 * black that crosses screen centre exactly on every *pip* instant and sweeps
 * fully across in one pip interval, so the bar-centre crossing lines up with
 * the tone pip from `avsyncaudiotestsrc`.
 *
 * Each frame also carries its frame index and a phase-locked CEA-708 caption
 * (alternating "TICK"/"TOCK") as ancillary data, so an `st2038extractor` can
 * split the ANC into its own flow and a downstream analyser can check that
 * video, audio and caption alignment survive the round trip.
 *
 * ## Example
 *
 * Eyeball-and-ear check — the bar crosses centre exactly on the beep:
 *
 * |[
 * gst-launch-1.0 \
 *   avsyncvideotestsrc is-live=true ! videoconvert ! autovideosink \
 *   avsyncaudiotestsrc is-live=true ! audioconvert ! autoaudiosink
 * ]|
 *
 * ## Phase locking
 *
 * The content is derived purely from each buffer's running time and the shared
 * `pip-interval`, so the two sources are aligned by construction and any skew
 * seen downstream was introduced by the pipeline under test. `pip-interval`
 * *must* be set to the same value on both elements. Set `is-live=true` to pace
 * output against the pipeline clock for live capture or transmit pipelines.
 */
use gst::glib;
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_base as gst_base;

mod imp;

glib::wrapper! {
    pub struct AvSyncVideoTestSrc(ObjectSubclass<imp::AvSyncVideoTestSrc>)
        @extends gst_base::PushSrc, gst_base::BaseSrc, gst::Element, gst::Object;
}

pub fn register(plugin: &gst::Plugin) -> Result<(), glib::BoolError> {
    gst::Element::register(
        Some(plugin),
        "avsyncvideotestsrc",
        gst::Rank::NONE,
        AvSyncVideoTestSrc::static_type(),
    )
}
