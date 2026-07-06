// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

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
