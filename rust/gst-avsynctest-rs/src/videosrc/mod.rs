// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

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
