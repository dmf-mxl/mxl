// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! `avsyncaudiotestsrc`: silence with a short tone pip centred on each pip
//! instant (`running_time` a multiple of the pip interval). Paired with
//! `avsyncvideotestsrc` it is phase-locked to the bar-centre crossing.
//!
//! Modelled on the gst-plugins-rs `sinesrc` tutorial element (live clock
//! waiting, timestamps from a sample counter). `num-buffers` is handled by
//! `GstBaseSrc`.

use std::f64::consts::PI;
use std::sync::LazyLock;
use std::sync::Mutex;

use gst::glib;
use gst::prelude::*;
use gst::subclass::prelude::*;
use gst_base::prelude::*;
use gst_base::subclass::base_src::CreateSuccess;
use gst_base::subclass::prelude::*;
use gstreamer as gst;
use gstreamer_audio as gst_audio;
use gstreamer_base as gst_base;

use crate::signal;

static CAT: LazyLock<gst::DebugCategory> = LazyLock::new(|| {
    gst::DebugCategory::new(
        "avsyncaudiotestsrc",
        gst::DebugColorFlags::empty(),
        Some("A/V sync test audio source"),
    )
});

#[derive(Debug, Clone, Copy)]
struct Settings {
    pip_interval: gst::ClockTime,
    pip_duration: gst::ClockTime,
    pip_freq: f64,
    pip_volume: f64,
    samples_per_buffer: u32,
    is_live: bool,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            pip_interval: signal::DEFAULT_PIP_INTERVAL,
            pip_duration: signal::DEFAULT_PIP_DURATION,
            pip_freq: signal::DEFAULT_PIP_FREQ_HZ,
            pip_volume: signal::DEFAULT_PIP_VOLUME,
            samples_per_buffer: signal::DEFAULT_SAMPLES_PER_BUFFER as u32,
            is_live: false,
        }
    }
}

#[derive(Default)]
struct State {
    info: Option<gst_audio::AudioInfo>,
    sample_offset: u64,
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
pub struct AvSyncAudioTestSrc {
    settings: Mutex<Settings>,
    state: Mutex<State>,
    clock_wait: Mutex<ClockWait>,
}

/// Tone amplitude at `running` for a pip signal, `0.0` in the silent gaps. The
/// pip at `running_time == 0` is skipped so every emitted pip is a full window.
fn pip_value(running: gst::ClockTime, settings: &Settings) -> f64 {
    let p = settings.pip_interval.nseconds();
    let half = settings.pip_duration.nseconds() / 2;
    let running_ns = running.nseconds();
    let k = (running_ns + p / 2) / p;
    let centre = k * p;
    if centre < half || running_ns.abs_diff(centre) >= half {
        return 0.0;
    }
    let t = running_ns as f64 / gst::ClockTime::SECOND.nseconds() as f64;
    settings.pip_volume * (2.0 * PI * settings.pip_freq * t).sin()
}

fn write_sample(format: gst_audio::AudioFormat, value: f64, dst: &mut [u8]) {
    if format == gst_audio::AudioFormat::F32le {
        dst.copy_from_slice(&(value as f32).to_le_bytes());
    } else if format == gst_audio::AudioFormat::S16be {
        let v = (value.clamp(-1.0, 1.0) * i16::MAX as f64) as i16;
        dst.copy_from_slice(&v.to_be_bytes());
    } else if format == gst_audio::AudioFormat::S24be {
        let v = (value.clamp(-1.0, 1.0) * 8_388_607.0) as i32;
        dst[0] = (v >> 16) as u8;
        dst[1] = (v >> 8) as u8;
        dst[2] = v as u8;
    }
}

impl AvSyncAudioTestSrc {
    fn fill(data: &mut [u8], info: &gst_audio::AudioInfo, sample_offset: u64, settings: &Settings) {
        let rate = info.rate() as u64;
        let channels = info.channels() as usize;
        let format = info.format();
        let bpf = info.bpf() as usize;
        let bytes_per_sample = bpf / channels;
        let second = gst::ClockTime::SECOND.nseconds() as u128;
        for (i, frame) in data.chunks_exact_mut(bpf).enumerate() {
            let g = sample_offset + i as u64;
            let running_ns = (g as u128 * second / rate as u128) as u64;
            let value = pip_value(gst::ClockTime::from_nseconds(running_ns), settings);
            for sample in frame.chunks_exact_mut(bytes_per_sample) {
                write_sample(format, value, sample);
            }
        }
    }
}

#[glib::object_subclass]
impl ObjectSubclass for AvSyncAudioTestSrc {
    const NAME: &'static str = "GstAvSyncAudioTestSrc";
    type Type = super::AvSyncAudioTestSrc;
    type ParentType = gst_base::PushSrc;
}

impl ObjectImpl for AvSyncAudioTestSrc {
    fn properties() -> &'static [glib::ParamSpec] {
        static PROPERTIES: LazyLock<Vec<glib::ParamSpec>> = LazyLock::new(|| {
            vec![
                glib::ParamSpecUInt64::builder("pip-interval")
                    .nick("Pip Interval")
                    .blurb("Nanoseconds between pips (must match the video source)")
                    .minimum(1)
                    .default_value(signal::DEFAULT_PIP_INTERVAL.nseconds())
                    .mutable_ready()
                    .build(),
                glib::ParamSpecUInt64::builder("pip-duration")
                    .nick("Pip Duration")
                    .blurb("Nanoseconds each tone pip lasts")
                    .minimum(1)
                    .default_value(signal::DEFAULT_PIP_DURATION.nseconds())
                    .mutable_ready()
                    .build(),
                glib::ParamSpecDouble::builder("pip-freq")
                    .nick("Pip Frequency")
                    .blurb("Tone frequency of the pip in Hz")
                    .minimum(1.0)
                    .default_value(signal::DEFAULT_PIP_FREQ_HZ)
                    .mutable_playing()
                    .build(),
                glib::ParamSpecDouble::builder("pip-volume")
                    .nick("Pip Volume")
                    .blurb("Peak amplitude of the pip (0.0-1.0)")
                    .minimum(0.0)
                    .maximum(1.0)
                    .default_value(signal::DEFAULT_PIP_VOLUME)
                    .mutable_playing()
                    .build(),
                glib::ParamSpecUInt::builder("samples-per-buffer")
                    .nick("Samples Per Buffer")
                    .blurb("Number of samples per output buffer")
                    .minimum(1)
                    .default_value(signal::DEFAULT_SAMPLES_PER_BUFFER as u32)
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
            "pip-duration" => {
                settings.pip_duration = gst::ClockTime::from_nseconds(value.get().unwrap());
            }
            "pip-freq" => settings.pip_freq = value.get().unwrap(),
            "pip-volume" => settings.pip_volume = value.get().unwrap(),
            "samples-per-buffer" => settings.samples_per_buffer = value.get().unwrap(),
            "is-live" => settings.is_live = value.get().unwrap(),
            _ => unimplemented!(),
        }
    }

    fn property(&self, _id: usize, pspec: &glib::ParamSpec) -> glib::Value {
        let settings = self.settings.lock().unwrap();
        match pspec.name() {
            "pip-interval" => settings.pip_interval.nseconds().to_value(),
            "pip-duration" => settings.pip_duration.nseconds().to_value(),
            "pip-freq" => settings.pip_freq.to_value(),
            "pip-volume" => settings.pip_volume.to_value(),
            "samples-per-buffer" => settings.samples_per_buffer.to_value(),
            "is-live" => settings.is_live.to_value(),
            _ => unimplemented!(),
        }
    }
}

impl GstObjectImpl for AvSyncAudioTestSrc {}

impl ElementImpl for AvSyncAudioTestSrc {
    fn metadata() -> Option<&'static gst::subclass::ElementMetadata> {
        static ELEMENT_METADATA: LazyLock<gst::subclass::ElementMetadata> = LazyLock::new(|| {
            gst::subclass::ElementMetadata::new(
                "A/V Sync Test Audio Source",
                "Source/Audio",
                "Emits tone pips phase-locked to avsyncvideotestsrc for A/V sync testing",
                "NVIDIA Corporation",
            )
        });

        Some(&*ELEMENT_METADATA)
    }

    fn pad_templates() -> &'static [gst::PadTemplate] {
        static PAD_TEMPLATES: LazyLock<Vec<gst::PadTemplate>> = LazyLock::new(|| {
            let caps = gst_audio::AudioCapsBuilder::new_interleaved()
                .format_list([
                    gst_audio::AudioFormat::F32le,
                    gst_audio::AudioFormat::S24be,
                    gst_audio::AudioFormat::S16be,
                ])
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

impl BaseSrcImpl for AvSyncAudioTestSrc {
    fn set_caps(&self, caps: &gst::Caps) -> Result<(), gst::LoggableError> {
        let info = gst_audio::AudioInfo::from_caps(caps).map_err(|_| {
            gst::loggable_error!(CAT, "Failed to build `AudioInfo` from caps {}", caps)
        })?;

        self.obj()
            .set_blocksize(info.bpf() * self.settings.lock().unwrap().samples_per_buffer);

        self.state.lock().unwrap().info = Some(info);

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
            let settings = *self.settings.lock().unwrap();
            let state = self.state.lock().unwrap();
            if let Some(ref info) = state.info {
                let latency = gst::ClockTime::SECOND
                    .mul_div_floor(settings.samples_per_buffer as u64, info.rate() as u64)
                    .unwrap();
                q.set(settings.is_live, latency, gst::ClockTime::NONE);
                return true;
            }
            return false;
        }
        BaseSrcImplExt::parent_query(self, query)
    }

    fn fixate(&self, mut caps: gst::Caps) -> gst::Caps {
        caps.truncate();
        {
            let caps = caps.make_mut();
            let s = caps.structure_mut(0).unwrap();
            s.fixate_field_nearest_int("rate", signal::DEFAULT_RATE);
            s.fixate_field_nearest_int("channels", signal::DEFAULT_CHANNELS);
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

impl PushSrcImpl for AvSyncAudioTestSrc {
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

        let n_samples = settings.samples_per_buffer as u64;
        let mut buffer = gst::Buffer::with_size(n_samples as usize * info.bpf() as usize).unwrap();
        {
            let buffer = buffer.get_mut().unwrap();
            let pts = state
                .sample_offset
                .mul_div_floor(gst::ClockTime::SECOND.nseconds(), info.rate() as u64)
                .map(gst::ClockTime::from_nseconds)
                .unwrap();
            let next_pts = (state.sample_offset + n_samples)
                .mul_div_floor(gst::ClockTime::SECOND.nseconds(), info.rate() as u64)
                .map(gst::ClockTime::from_nseconds)
                .unwrap();
            buffer.set_pts(pts);
            buffer.set_duration(next_pts - pts);

            let mut map = buffer.map_writable().unwrap();
            Self::fill(map.as_mut_slice(), &info, state.sample_offset, &settings);
        }
        state.sample_offset += n_samples;
        drop(state);

        self.sync_to_clock(&buffer)?;

        Ok(CreateSuccess::NewBuffer(buffer))
    }
}

impl AvSyncAudioTestSrc {
    /// In live mode, wait on the pipeline clock until the last sample's running
    /// time, cancellable from `unlock()` (see `sinesrc`).
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
