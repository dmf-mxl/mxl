// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * SECTION:element-avsyncaudiotestsrc
 * @see_also: avsyncvideotestsrc
 *
 * `avsyncaudiotestsrc` is one half of a phase-locked audio/video test pair for
 * measuring end-to-end A/V synchronisation. It emits silence with a short tone
 * pip centred on every *pip* instant, phase-locked to the bar-centre crossing
 * of `avsyncvideotestsrc`.
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
    pub struct AvSyncAudioTestSrc(ObjectSubclass<imp::AvSyncAudioTestSrc>)
        @extends gst_base::PushSrc, gst_base::BaseSrc, gst::Element, gst::Object;
}

pub fn register(plugin: &gst::Plugin) -> Result<(), glib::BoolError> {
    gst::Element::register(
        Some(plugin),
        "avsyncaudiotestsrc",
        gst::Rank::NONE,
        AvSyncAudioTestSrc::static_type(),
    )
}
