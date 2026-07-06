// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

//! `avsyncvideotestsrc`: a white vertical bar on black that is at screen centre
//! exactly at each pip instant (`running_time` a multiple of the pip interval)
//! and sweeps across in one interval. The frame index is written into payload
//! byte 0 and into a `GstAncillaryMeta` (DID/SDID in the user space), so an
//! `st2038extractor` can split off a matching data flow.
//!
//! Emits v210 or UYVP. Modelled on the gst-plugins-rs `sinesrc` tutorial
//! element; `num-buffers` is handled by `GstBaseSrc`.

use std::sync::LazyLock;
use std::sync::Mutex;

use gst::glib;
use gst::prelude::*;
use gst::subclass::prelude::*;
use gst_base::prelude::*;
use gst_base::subclass::base_src::CreateSuccess;
use gst_base::subclass::prelude::*;
use gstreamer as gst;
use gstreamer_base as gst_base;
use gstreamer_video as gst_video;

use crate::signal;

static CAT: LazyLock<gst::DebugCategory> = LazyLock::new(|| {
    gst::DebugCategory::new(
        "avsyncvideotestsrc",
        gst::DebugColorFlags::empty(),
        Some("A/V sync test video source"),
    )
});

#[derive(Debug, Clone, Copy)]
struct Settings {
    pip_interval: gst::ClockTime,
    width: i32,
    height: i32,
    framerate: gst::Fraction,
    is_live: bool,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            pip_interval: signal::DEFAULT_PIP_INTERVAL,
            width: signal::DEFAULT_WIDTH,
            height: signal::DEFAULT_HEIGHT,
            framerate: gst::Fraction::new(
                signal::DEFAULT_FRAMERATE_NUM,
                signal::DEFAULT_FRAMERATE_DEN,
            ),
            is_live: false,
        }
    }
}

#[derive(Default)]
struct State {
    info: Option<gst_video::VideoInfo>,
    n_frames: u64,
    bar_width: u32,
}

struct ClockWait {
    clock_id: Option<gst::SingleShotClockId>,
    flushing: bool,
}

impl Default for ClockWait {
    fn default() -> ClockWait {
        ClockWait {
            clock_id: None,
            flushing: true,
        }
    }
}

#[derive(Default)]
pub struct AvSyncVideoTestSrc {
    settings: Mutex<Settings>,
    state: Mutex<State>,
    clock_wait: Mutex<ClockWait>,
}

fn luma_at(x: usize, width: u32, bar_centre: u32, bar_width: u32) -> u16 {
    if x >= width as usize {
        return signal::LUMA_BLACK;
    }
    if signal::circular_distance(x as u32, bar_centre, width) <= bar_width / 2 {
        signal::LUMA_WHITE
    } else {
        signal::LUMA_BLACK
    }
}

/// Pack three 10-bit components into one little-endian v210 word.
fn v210_word(a: u16, b: u16, c: u16) -> u32 {
    (a as u32) | ((b as u32) << 10) | ((c as u32) << 20)
}

fn fill_v210(data: &mut [u8], info: &gst_video::VideoInfo, bar_centre: u32, bar_width: u32) {
    let width = info.width();
    let stride = info.stride()[0] as usize;
    let groups = width.div_ceil(6) as usize;
    let c = signal::CHROMA_NEUTRAL;
    for y in 0..info.height() as usize {
        let row = &mut data[y * stride..][..stride];
        for g in 0..groups {
            let l = |i: usize| luma_at(6 * g + i, width, bar_centre, bar_width);
            let words = [
                v210_word(c, l(0), c),
                v210_word(l(1), c, l(2)),
                v210_word(c, l(3), c),
                v210_word(l(4), c, l(5)),
            ];
            for (w, chunk) in words.iter().zip(row[g * 16..].chunks_exact_mut(4)) {
                chunk.copy_from_slice(&w.to_le_bytes());
            }
        }
    }
}

fn fill_uyvp(data: &mut [u8], info: &gst_video::VideoInfo, bar_centre: u32, bar_width: u32) {
    let width = info.width();
    let stride = info.stride()[0] as usize;
    let pairs = width.div_ceil(2) as usize;
    let c = signal::CHROMA_NEUTRAL as u64;
    for y in 0..info.height() as usize {
        let row = &mut data[y * stride..][..stride];
        for p in 0..pairs {
            let y0 = luma_at(2 * p, width, bar_centre, bar_width) as u64;
            let y1 = luma_at(2 * p + 1, width, bar_centre, bar_width) as u64;
            // MSB-first 40-bit [U, Y0, V, Y1] over 5 bytes (GStreamer UYVP).
            let val = (c << 30) | (y0 << 20) | (c << 10) | y1;
            let base = p * 5;
            row[base] = (val >> 32) as u8;
            row[base + 1] = (val >> 24) as u8;
            row[base + 2] = (val >> 16) as u8;
            row[base + 3] = (val >> 8) as u8;
            row[base + 4] = val as u8;
        }
    }
}

fn extend_with_even_odd_parity(v: u8) -> u16 {
    if v.count_ones() & 1 == 0 {
        0x1_00 | (v as u16)
    } else {
        0x2_00 | (v as u16)
    }
}

fn compute_checksum(did_10bit: u16, sdid_10bit: u16, dc_10bit: u16, data: &[u16]) -> u16 {
    let mut checksum = 0u16;
    checksum = checksum.wrapping_add(did_10bit & 0x1ff);
    checksum = checksum.wrapping_add(sdid_10bit & 0x1ff);
    checksum = checksum.wrapping_add(dc_10bit & 0x1ff);
    for &w in data {
        checksum = checksum.wrapping_add(w & 0x1ff);
    }
    checksum &= 0x1ff;
    checksum |= ((!(checksum >> 8)) & 0x01) << 9;
    checksum
}

/// Attach one `GstAncillaryMeta` carrying `frame_idx`, as `st2038extractor`
/// expects (10-bit even/odd-parity words plus checksum).
fn add_frame_index_ancillary(buffer: &mut gst::BufferRef, frame_idx: u8) {
    let payload = [frame_idx];
    let mut meta = gst_video::video_meta::AncillaryMeta::add(buffer);
    meta.set_c_not_y_channel(false);
    meta.set_line(signal::ANC_LINE);
    meta.set_offset(signal::ANC_OFFSET);
    let did_10bit = extend_with_even_odd_parity(signal::ANC_DID);
    let sdid_10bit = extend_with_even_odd_parity(signal::ANC_SDID);
    let dc_10bit = extend_with_even_odd_parity(payload.len() as u8);
    meta.set_did(did_10bit);
    meta.set_sdid_block_number(sdid_10bit);
    let data: Vec<u16> = payload
        .iter()
        .copied()
        .map(extend_with_even_odd_parity)
        .collect();
    meta.set_checksum(compute_checksum(did_10bit, sdid_10bit, dc_10bit, &data));
    meta.set_data(glib::Slice::from(data));
}

#[glib::object_subclass]
impl ObjectSubclass for AvSyncVideoTestSrc {
    const NAME: &'static str = "GstAvSyncVideoTestSrc";
    type Type = super::AvSyncVideoTestSrc;
    type ParentType = gst_base::PushSrc;
}

impl ObjectImpl for AvSyncVideoTestSrc {
    fn properties() -> &'static [glib::ParamSpec] {
        static PROPERTIES: LazyLock<Vec<glib::ParamSpec>> = LazyLock::new(|| {
            vec![
                glib::ParamSpecUInt64::builder("pip-interval")
                    .nick("Pip Interval")
                    .blurb("Nanoseconds between bar-centre crossings (must match the audio source)")
                    .minimum(1)
                    .default_value(signal::DEFAULT_PIP_INTERVAL.nseconds())
                    .mutable_ready()
                    .build(),
                glib::ParamSpecInt::builder("width")
                    .nick("Width")
                    .blurb("Default output width when downstream leaves it open")
                    .minimum(1)
                    .default_value(signal::DEFAULT_WIDTH)
                    .mutable_ready()
                    .build(),
                glib::ParamSpecInt::builder("height")
                    .nick("Height")
                    .blurb("Default output height when downstream leaves it open")
                    .minimum(1)
                    .default_value(signal::DEFAULT_HEIGHT)
                    .mutable_ready()
                    .build(),
                gst::ParamSpecFraction::builder("framerate")
                    .nick("Framerate")
                    .blurb("Default output framerate when downstream leaves it open")
                    .minimum(gst::Fraction::new(1, 1))
                    .maximum(gst::Fraction::new(i32::MAX, 1))
                    .default_value(gst::Fraction::new(
                        signal::DEFAULT_FRAMERATE_NUM,
                        signal::DEFAULT_FRAMERATE_DEN,
                    ))
                    .mutable_ready()
                    .build(),
                glib::ParamSpecBoolean::builder("is-live")
                    .nick("Is Live")
                    .blurb("Whether to pace output against the pipeline clock")
                    .default_value(false)
                    .mutable_ready()
                    .build(),
            ]
        });

        PROPERTIES.as_ref()
    }

    fn constructed(&self) {
        self.parent_constructed();
        let obj = self.obj();
        obj.set_live(false);
        obj.set_format(gst::Format::Time);
    }

    fn set_property(&self, _id: usize, value: &glib::Value, pspec: &glib::ParamSpec) {
        let mut settings = self.settings.lock().unwrap();
        match pspec.name() {
            "pip-interval" => {
                settings.pip_interval = gst::ClockTime::from_nseconds(value.get().unwrap());
            }
            "width" => settings.width = value.get().unwrap(),
            "height" => settings.height = value.get().unwrap(),
            "framerate" => settings.framerate = value.get().unwrap(),
            "is-live" => settings.is_live = value.get().unwrap(),
            _ => unimplemented!(),
        }
    }

    fn property(&self, _id: usize, pspec: &glib::ParamSpec) -> glib::Value {
        let settings = self.settings.lock().unwrap();
        match pspec.name() {
            "pip-interval" => settings.pip_interval.nseconds().to_value(),
            "width" => settings.width.to_value(),
            "height" => settings.height.to_value(),
            "framerate" => settings.framerate.to_value(),
            "is-live" => settings.is_live.to_value(),
            _ => unimplemented!(),
        }
    }
}

impl GstObjectImpl for AvSyncVideoTestSrc {}

impl ElementImpl for AvSyncVideoTestSrc {
    fn metadata() -> Option<&'static gst::subclass::ElementMetadata> {
        static ELEMENT_METADATA: LazyLock<gst::subclass::ElementMetadata> = LazyLock::new(|| {
            gst::subclass::ElementMetadata::new(
                "A/V Sync Test Video Source",
                "Source/Video",
                "Emits a sweeping bar phase-locked to avsyncaudiotestsrc for A/V sync testing",
                "NVIDIA Corporation",
            )
        });

        Some(&*ELEMENT_METADATA)
    }

    fn pad_templates() -> &'static [gst::PadTemplate] {
        static PAD_TEMPLATES: LazyLock<Vec<gst::PadTemplate>> = LazyLock::new(|| {
            let caps = gst_video::VideoCapsBuilder::new()
                .format_list([gst_video::VideoFormat::V210, gst_video::VideoFormat::Uyvp])
                .build();
            let src_pad_template = gst::PadTemplate::new(
                "src",
                gst::PadDirection::Src,
                gst::PadPresence::Always,
                &caps,
            )
            .unwrap();

            vec![src_pad_template]
        });

        PAD_TEMPLATES.as_ref()
    }

    fn change_state(
        &self,
        transition: gst::StateChange,
    ) -> Result<gst::StateChangeSuccess, gst::StateChangeError> {
        if let gst::StateChange::ReadyToPaused = transition {
            self.obj().set_live(self.settings.lock().unwrap().is_live);
        }
        self.parent_change_state(transition)
    }
}

impl BaseSrcImpl for AvSyncVideoTestSrc {
    fn set_caps(&self, caps: &gst::Caps) -> Result<(), gst::LoggableError> {
        let info = gst_video::VideoInfo::from_caps(caps).map_err(|_| {
            gst::loggable_error!(CAT, "Failed to build `VideoInfo` from caps {}", caps)
        })?;
        if !matches!(
            info.format(),
            gst_video::VideoFormat::V210 | gst_video::VideoFormat::Uyvp
        ) {
            return Err(gst::loggable_error!(
                CAT,
                "Unsupported video format (expected v210 or UYVP): {:?}",
                info.format()
            ));
        }

        let pip_interval = self.settings.lock().unwrap().pip_interval;
        // Bar width is one frame's horizontal step, so the sweep tiles gap-free
        // and the bar thins as the frame rate rises.
        let fps = info.fps().numer() as f64 / info.fps().denom() as f64;
        let frames_per_interval =
            (pip_interval.nseconds() as f64 / gst::ClockTime::SECOND.nseconds() as f64) * fps;
        let bar_width = (info.width() as f64 / frames_per_interval).round().max(1.0) as u32;

        let mut state = self.state.lock().unwrap();
        state.bar_width = bar_width;
        state.info = Some(info);
        drop(state);

        let _ = self
            .obj()
            .post_message(gst::message::Latency::builder().src(&*self.obj()).build());

        Ok(())
    }

    fn start(&self) -> Result<(), gst::ErrorMessage> {
        *self.state.lock().unwrap() = Default::default();
        self.unlock_stop()?;
        Ok(())
    }

    fn stop(&self) -> Result<(), gst::ErrorMessage> {
        *self.state.lock().unwrap() = Default::default();
        self.unlock()?;
        Ok(())
    }

    fn query(&self, query: &mut gst::QueryRef) -> bool {
        if let gst::QueryViewMut::Latency(q) = query.view_mut() {
            let is_live = self.settings.lock().unwrap().is_live;
            let state = self.state.lock().unwrap();
            if let Some(ref info) = state.info {
                let latency = gst::ClockTime::SECOND
                    .mul_div_floor(info.fps().denom() as u64, info.fps().numer() as u64)
                    .unwrap();
                q.set(is_live, latency, gst::ClockTime::NONE);
                return true;
            }
            return false;
        }
        BaseSrcImplExt::parent_query(self, query)
    }

    fn fixate(&self, mut caps: gst::Caps) -> gst::Caps {
        let settings = *self.settings.lock().unwrap();
        caps.truncate();
        {
            let caps = caps.make_mut();
            let s = caps.structure_mut(0).unwrap();
            s.fixate_field_nearest_int("width", settings.width);
            s.fixate_field_nearest_int("height", settings.height);
            s.fixate_field_nearest_fraction("framerate", settings.framerate);
        }
        self.parent_fixate(caps)
    }

    fn is_seekable(&self) -> bool {
        false
    }

    fn unlock(&self) -> Result<(), gst::ErrorMessage> {
        let mut clock_wait = self.clock_wait.lock().unwrap();
        if let Some(clock_id) = clock_wait.clock_id.take() {
            clock_id.unschedule();
        }
        clock_wait.flushing = true;
        Ok(())
    }

    fn unlock_stop(&self) -> Result<(), gst::ErrorMessage> {
        self.clock_wait.lock().unwrap().flushing = false;
        Ok(())
    }
}

impl PushSrcImpl for AvSyncVideoTestSrc {
    fn create(
        &self,
        _buffer: Option<&mut gst::BufferRef>,
    ) -> Result<CreateSuccess, gst::FlowError> {
        let settings = *self.settings.lock().unwrap();

        let mut state = self.state.lock().unwrap();
        let info = match state.info {
            None => {
                gst::element_imp_error!(self, gst::CoreError::Negotiation, ["Have no caps yet"]);
                return Err(gst::FlowError::NotNegotiated);
            }
            Some(ref info) => info.clone(),
        };
        let bar_width = state.bar_width;
        let n = state.n_frames;

        let num = info.fps().numer() as u64;
        let den = info.fps().denom() as u64;
        let pts = gst::ClockTime::SECOND.mul_div_floor(n * den, num).unwrap();
        let next_pts = gst::ClockTime::SECOND
            .mul_div_floor((n + 1) * den, num)
            .unwrap();
        let bar_centre =
            signal::bar_centre_column(signal::phase(pts, settings.pip_interval), info.width());
        let frame_idx = n as u8;

        let mut buffer = gst::Buffer::with_size(info.size()).unwrap();
        {
            let buffer = buffer.get_mut().unwrap();
            buffer.set_pts(pts);
            buffer.set_duration(next_pts - pts);
            {
                let mut map = buffer.map_writable().unwrap();
                let data = map.as_mut_slice();
                match info.format() {
                    gst_video::VideoFormat::V210 => fill_v210(data, &info, bar_centre, bar_width),
                    gst_video::VideoFormat::Uyvp => fill_uyvp(data, &info, bar_centre, bar_width),
                    _ => unreachable!("format validated in set_caps"),
                }
                data[0] = frame_idx;
            }
            add_frame_index_ancillary(buffer, frame_idx);
        }
        state.n_frames += 1;
        drop(state);

        self.sync_to_clock(&buffer)?;

        Ok(CreateSuccess::NewBuffer(buffer))
    }
}

impl AvSyncVideoTestSrc {
    /// In live mode, wait on the pipeline clock until this frame's running time,
    /// cancellable from `unlock()` (see `sinesrc`).
    fn sync_to_clock(&self, buffer: &gst::Buffer) -> Result<(), gst::FlowError> {
        if !self.obj().is_live() {
            return Ok(());
        }
        let Some((clock, base_time)) = Option::zip(self.obj().clock(), self.obj().base_time())
        else {
            return Ok(());
        };
        let segment = self
            .obj()
            .segment()
            .downcast::<gst::format::Time>()
            .unwrap();
        let running_time = segment.to_running_time(buffer.pts().opt_add(buffer.duration()));
        let Some(wait_until) = running_time.opt_add(base_time) else {
            return Ok(());
        };

        let mut clock_wait = self.clock_wait.lock().unwrap();
        if clock_wait.flushing {
            return Err(gst::FlowError::Flushing);
        }
        let id = clock.new_single_shot_id(wait_until);
        clock_wait.clock_id = Some(id.clone());
        drop(clock_wait);

        let (res, _) = id.wait();
        self.clock_wait.lock().unwrap().clock_id.take();
        if res == Err(gst::ClockError::Unscheduled) {
            return Err(gst::FlowError::Flushing);
        }
        Ok(())
    }
}
