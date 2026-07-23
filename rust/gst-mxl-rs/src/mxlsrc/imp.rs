// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// Copyright (C) 2018 Sebastian Dröge <sebastian@centricular.com>
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use gst::glib;
use gst::prelude::*;
use gst::subclass::prelude::*;
use gst_base::prelude::*;
use gst_base::subclass::base_src::CreateSuccess;
use gst_base::subclass::prelude::*;
use gstreamer as gst;
use gstreamer::Buffer;
use gstreamer_base as gst_base;
use tracing::trace;

use std::sync::LazyLock;
use std::sync::Mutex;
use std::time::Duration;

use crate::clock::ClockOffsetExt;
use crate::mxlsrc;
use crate::mxlsrc::create_continuous::create_continuous;
use crate::mxlsrc::create_discrete::create_discrete;
use crate::mxlsrc::mxl_helper;
use crate::mxlsrc::state::Context;
use crate::mxlsrc::state::DEFAULT_DOMAIN;
use crate::mxlsrc::state::DEFAULT_FLOW_ID;
use crate::mxlsrc::state::FlowState;
use crate::mxlsrc::state::Settings;
use crate::mxlsrc::timing;

pub(crate) static CAT: LazyLock<gst::DebugCategory> = LazyLock::new(|| {
    gst::DebugCategory::new("mxlsrc", gst::DebugColorFlags::empty(), Some("MXL Source"))
});

pub(crate) struct ClockWait {
    clock_id: Option<gst::SingleShotClockId>,
    pub(crate) flushing: bool,
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
pub struct MxlSrc {
    pub(crate) settings: Mutex<Settings>,
    pub(crate) context: Mutex<Context>,
    pub(crate) clock_wait: Mutex<ClockWait>,
    shared_clock_offset: Mutex<Option<crate::clock::SharedClockOffset>>,
}

pub enum CreateState {
    DataCreated(Buffer),
    NoDataCreated,
}

#[glib::object_subclass]
impl ObjectSubclass for MxlSrc {
    const NAME: &'static str = "GstRsMxlSrc";
    type Type = mxlsrc::MxlSrc;
    type ParentType = gst_base::PushSrc;
}

impl ObjectImpl for MxlSrc {
    fn properties() -> &'static [glib::ParamSpec] {
        static PROPERTIES: LazyLock<Vec<glib::ParamSpec>> = LazyLock::new(|| {
            vec![
                glib::ParamSpecString::builder("video-flow-id")
                    .nick("VideoFlowID")
                    .blurb("Video Flow ID")
                    .default_value(DEFAULT_FLOW_ID)
                    .mutable_ready()
                    .build(),
                glib::ParamSpecString::builder("audio-flow-id")
                    .nick("AudioFlowID")
                    .blurb("Audio Flow ID")
                    .default_value(DEFAULT_FLOW_ID)
                    .mutable_ready()
                    .build(),
                glib::ParamSpecString::builder("data-flow-id")
                    .nick("DataFlowID")
                    .blurb("Data Flow ID")
                    .default_value(DEFAULT_FLOW_ID)
                    .mutable_ready()
                    .build(),
                glib::ParamSpecString::builder("domain")
                    .nick("Domain")
                    .blurb("Domain")
                    .default_value(DEFAULT_DOMAIN)
                    .mutable_ready()
                    .build(),
            ]
        });

        PROPERTIES.as_ref()
    }

    fn constructed(&self) {
        self.parent_constructed();
        #[cfg(feature = "tracing")]
        {
            use tracing_subscriber::filter::LevelFilter;
            use tracing_subscriber::util::SubscriberInitExt;
            let _ = tracing_subscriber::fmt()
                .compact()
                .with_file(true)
                .with_line_number(true)
                .with_thread_ids(true)
                .with_target(false)
                .with_max_level(LevelFilter::TRACE)
                .with_ansi(true)
                .finish()
                .try_init();
        }
        let obj = self.obj();
        obj.set_live(true);
        obj.set_format(gst::Format::Time);
    }

    fn set_property(&self, _id: usize, value: &glib::Value, pspec: &glib::ParamSpec) {
        if let Ok(mut settings) = self.settings.lock() {
            match pspec.name() {
                "video-flow-id" => {
                    if let Ok(flow_id) = value.get::<String>() {
                        settings.video_flow = Some(flow_id);
                    } else {
                        gst::error!(CAT, imp = self, "Invalid type for video-flow-id property");
                    }
                }
                "audio-flow-id" => {
                    if let Ok(flow_id) = value.get::<String>() {
                        settings.audio_flow = Some(flow_id);
                    } else {
                        gst::error!(CAT, imp = self, "Invalid type for audio-flow-id property");
                    }
                }
                "data-flow-id" => {
                    if let Ok(flow_id) = value.get::<String>() {
                        settings.data_flow = Some(flow_id);
                    } else {
                        gst::error!(CAT, imp = self, "Invalid type for data-flow-id property");
                    }
                }
                "domain" => {
                    if let Ok(domain) = value.get::<String>() {
                        gst::info!(
                            CAT,
                            imp = self,
                            "Changing domain from {} to {}",
                            settings.domain,
                            domain
                        );
                        settings.domain = domain;
                    } else {
                        gst::error!(CAT, imp = self, "Invalid type for domain property");
                    }
                }
                other => {
                    gst::error!(CAT, imp = self, "Unknown property '{}'", other);
                }
            }
        } else {
            gst::error!(
                CAT,
                imp = self,
                "Settings mutex poisoned — property change ignored"
            );
        }
    }

    fn property(&self, _id: usize, pspec: &glib::ParamSpec) -> glib::Value {
        if let Ok(settings) = self.settings.lock() {
            match pspec.name() {
                "video-flow-id" => settings.video_flow.to_value(),
                "audio-flow-id" => settings.audio_flow.to_value(),
                "data-flow-id" => settings.data_flow.to_value(),
                "domain" => settings.domain.to_value(),
                _ => {
                    gst::error!(CAT, imp = self, "Unknown property {}", pspec.name());
                    glib::Value::from(&"")
                }
            }
        } else {
            gst::error!(CAT, imp = self, "Settings mutex poisoned");
            glib::Value::from(&"")
        }
    }
}

impl GstObjectImpl for MxlSrc {}

impl ElementImpl for MxlSrc {
    fn metadata() -> Option<&'static gst::subclass::ElementMetadata> {
        static ELEMENT_METADATA: LazyLock<gst::subclass::ElementMetadata> = LazyLock::new(|| {
            gst::subclass::ElementMetadata::new(
                "MXL Source",
                "Source/Video",
                "Reads GStreamer buffers from an MXL flow",
                "Contributors to the Media eXchange Layer project",
            )
        });

        Some(&*ELEMENT_METADATA)
    }

    fn pad_templates() -> &'static [gst::PadTemplate] {
        static PAD_TEMPLATES: LazyLock<Result<Vec<gst::PadTemplate>, glib::BoolError>> =
            LazyLock::new(|| {
                let mut caps = gst::Caps::new_empty();
                {
                    let caps_mut = caps.make_mut();

                    caps_mut.append(
                        gst::Caps::builder("video/x-raw")
                            .field("format", "v210")
                            .build(),
                    );
                    caps.make_mut().append(
                        gst::Caps::builder("audio/x-raw")
                            .field("format", "F32LE")
                            .build(),
                    );
                    caps.make_mut().append(
                        gst::Caps::builder("meta/x-st-2038")
                            .field("alignment", "frame")
                            .build(),
                    );
                }
                let src_pad_template = gst::PadTemplate::new(
                    "src",
                    gst::PadDirection::Src,
                    gst::PadPresence::Always,
                    &caps,
                )?;

                Ok(vec![src_pad_template])
            });

        match PAD_TEMPLATES.as_ref() {
            Ok(templates) => templates,
            Err(err) => {
                trace!("Failed to create src pad template: {:?}", err);
                &[]
            }
        }
    }

    fn change_state(
        &self,
        transition: gst::StateChange,
    ) -> Result<gst::StateChangeSuccess, gst::StateChangeError> {
        self.parent_change_state(transition)
    }

    fn set_clock(&self, clock: Option<&gst::Clock>) -> bool {
        self.handle_set_clock(clock)
    }

    fn set_context(&self, context: &gst::Context) {
        self.handle_set_context(context);
    }
}

impl crate::clock::ClockOffsetExt for MxlSrc {
    fn mxl_now(&self) -> Result<Option<u64>, crate::clock::ClockOffsetError> {
        let context = self
            .context
            .lock()
            .map_err(|_| crate::clock::ClockOffsetError::Failed)?;
        let instance = context
            .instance
            .as_ref()
            .or(context.state.as_ref().map(|s| &s.instance));
        Ok(instance.map(|instance| instance.get_time()))
    }

    fn shared_clock_offset(&self) -> &Mutex<Option<crate::clock::SharedClockOffset>> {
        &self.shared_clock_offset
    }
}

impl BaseSrcImpl for MxlSrc {
    fn event(&self, event: &gst::Event) -> bool {
        self.parent_event(event)
    }

    fn negotiate(&self) -> Result<(), gst::LoggableError> {
        gst::info!(CAT, imp = self, "Negotiating caps…");

        {
            let settings = self
                .settings
                .lock()
                .map_err(|e| gst::loggable_error!(CAT, "Failed to lock settings mutex {}", e))?;
            if settings.audio_flow.is_some() && settings.video_flow.is_some() {
                gst::warning!(CAT, imp = self, "You can't set both video and audio flows");
                return self.parent_negotiate();
            }
            let flow_id_count = settings.flow_id_count();
            if flow_id_count > 1 {
                gst::warning!(
                    CAT,
                    imp = self,
                    "Set exactly one of video-flow-id, audio-flow-id, or data-flow-id"
                );
                return self.parent_negotiate();
            }
            if settings.domain.is_empty() || flow_id_count == 0 {
                gst::warning!(CAT, imp = self, "domain or flow-id not set yet");
                return self.parent_negotiate();
            }
        }

        // `start()` does not attach the MXL reader (so PLAYING is reachable
        // before the producer creates the flow). Attach here on the streaming
        // thread. `init_mxl_reader` polls until the flow exists; `unlock()` sets
        // `clock_wait.flushing` so teardown can interrupt that wait.
        let need_init = {
            let context = self
                .context
                .lock()
                .map_err(|e| gst::loggable_error!(CAT, "Failed to lock context mutex {}", e))?;
            context
                .state
                .as_ref()
                .is_none_or(|s| s.flow_state.is_none())
        };
        if need_init {
            mxl_helper::init(self)
                .map_err(|e| gst::loggable_error!(CAT, "Failed to attach flow: {}", e))?;
            // The flow's grain rate (hence our live latency) is only known after
            // attach; ask the pipeline to recompute latency with the real value.
            let _ = self
                .obj()
                .post_message(gst::message::Latency::builder().src(&*self.obj()).build());
        }

        let settings = self
            .settings
            .lock()
            .map_err(|e| gst::loggable_error!(CAT, "Failed to lock settings mutex {}", e))?;
        let context = self
            .context
            .lock()
            .map_err(|e| gst::loggable_error!(CAT, "Failed to lock context mutex {}", e))?;
        let instance = &context
            .state
            .as_ref()
            .ok_or(gst::loggable_error!(CAT, "Failed to get state"))?
            .instance;
        let flow_id = mxl_helper::get_flow_type_id(&settings)?;
        let json_flow_description = mxl_helper::get_mxl_flow_json(instance, flow_id)?;
        let flow_description = mxl_helper::get_flow_def(self, json_flow_description)?;
        mxl_helper::set_json_caps(self, flow_description)
    }

    fn set_caps(&self, caps: &gst::Caps) -> Result<(), gst::LoggableError> {
        let structure = caps
            .structure(0)
            .ok_or_else(|| gst::loggable_error!(CAT, "No structure in caps {}", caps))?;
        let name = structure.name();

        if name == "video/x-raw" {
            let format = structure
                .get::<String>("format")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to set caps {}", e))?;
            if format != "v210" {
                return Err(gst::loggable_error!(
                    CAT,
                    "Unsupported video format (expected v210): {}",
                    format
                ));
            }
            let width = structure
                .get::<i32>("width")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to set caps {}", e))?;
            let height = structure
                .get::<i32>("height")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to set caps {}", e))?;
            let framerate = structure
                .get::<gst::Fraction>("framerate")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to set caps {}", e))?;
            let interlace_mode = structure
                .get::<String>("interlace-mode")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to set caps {}", e))?;
            let colorimetry = structure
                .get::<String>("colorimetry")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to set caps {}", e))?;

            trace!(
                "Negotiated caps: format={} {}x{} @ {}/{}fps, interlace={}, colorimetry={}",
                format,
                width,
                height,
                framerate.numer(),
                framerate.denom(),
                interlace_mode,
                colorimetry,
            );

            Ok(())
        } else if name == "audio/x-raw" {
            let format = structure
                .get::<String>("format")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to get format from caps: {}", e))?;
            if format != "F32LE" {
                return Err(gst::loggable_error!(
                    CAT,
                    "Unsupported audio format (expected F32LE): {}",
                    format
                ));
            }
            let rate = structure
                .get::<i32>("rate")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to get rate from caps: {}", e))?;

            let channels = structure.get::<i32>("channels").map_err(|e| {
                gst::loggable_error!(CAT, "Failed to get channels from caps: {}", e)
            })?;

            trace!(
                "Negotiated caps: format={}, rate={}, channel_count={} ",
                format, rate, channels
            );

            Ok(())
        } else if name == "meta/x-st-2038" {
            let framerate = structure
                .get::<gst::Fraction>("framerate")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to set caps {}", e))?;
            let alignment = structure
                .get::<String>("alignment")
                .unwrap_or_else(|_| "<missing>".to_owned());
            trace!(
                "Negotiated caps: meta/x-st-2038 @ {}/{}, alignment={}",
                framerate.numer(),
                framerate.denom(),
                alignment
            );
            Ok(())
        } else {
            Err(gst::loggable_error!(
                CAT,
                "Unsupported caps structure: {}",
                caps
            ))
        }
    }

    fn start(&self) -> Result<(), gst::ErrorMessage> {
        // Create the domain instance and clock now so the clock is available
        // when the pipeline selects one. This is non-blocking — unlike the
        // reader attach (which can wait for `FlowNotFound`), creating the
        // instance never waits for the flow, so it is safe on the state-change
        // thread. The reader attach still runs from `negotiate()`. If the domain
        // was unset here, `negotiate()` → `init()` calls `ensure_instance()` once
        // domain and flow-id are ready. No race: `ensure_instance` caches under
        // `context` lock and concurrent callers reuse the winner.
        let have_domain = {
            let settings = self.settings.lock().map_err(|e| {
                gst::error_msg!(gst::CoreError::Failed, ["Failed to lock settings: {}", e])
            })?;
            !settings.domain.is_empty()
        };
        if have_domain {
            mxl_helper::ensure_instance(self)?;
        }

        // Adopt the pipeline-shared offset cell now, during the sequential
        // READY->PAUSED state change, so both mxlsrcs deterministically share
        // one `D` (see clock.rs) and expose an identical PTS for a given
        // absolute index. Establishing it lazily on the first buffer races.
        self.ensure_clock_offset()
            .map_err(|_| crate::clock::ClockOffsetError::Failed.into_error_message())?;

        self.unlock_stop()?;
        gst::info!(CAT, imp = self, "Started");

        Ok(())
    }

    fn stop(&self) -> Result<(), gst::ErrorMessage> {
        let mut context = self.context.lock().map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to get settings mutex: {}", e]
            )
        })?;

        *context = Default::default();

        self.unlock()?;

        gst::info!(CAT, imp = self, "Stopped");

        Ok(())
    }

    fn query(&self, query: &mut gst::QueryRef) -> bool {
        if let gst::QueryViewMut::Latency(q) = query.view_mut()
            && let Some(latency) = self.live_latency()
        {
            q.set(true, latency, gst::ClockTime::NONE);
            return true;
        }
        BaseSrcImplExt::parent_query(self, query)
    }

    fn fixate(&self, caps: gst::Caps) -> gst::Caps {
        self.parent_fixate(caps)
    }

    fn unlock(&self) -> Result<(), gst::ErrorMessage> {
        gst::debug!(CAT, imp = self, "Unlocking");
        let mut clock_wait = self.clock_wait.lock().map_err(|e| {
            gst::error_msg!(gst::CoreError::Failed, ["Failed to lock clock: {}", e])
        })?;
        if let Some(clock_id) = clock_wait.clock_id.take() {
            clock_id.unschedule();
        }
        clock_wait.flushing = true;

        Ok(())
    }

    fn unlock_stop(&self) -> Result<(), gst::ErrorMessage> {
        gst::debug!(CAT, imp = self, "Unlock stop");
        let mut clock_wait = self.clock_wait.lock().map_err(|e| {
            gst::error_msg!(gst::CoreError::Failed, ["Failed to lock clock: {}", e])
        })?;
        clock_wait.flushing = false;

        Ok(())
    }
}

impl PushSrcImpl for MxlSrc {
    fn create(
        &self,
        _buffer: Option<&mut gst::BufferRef>,
    ) -> Result<CreateSuccess, gst::FlowError> {
        loop {
            // Establish the pipeline-shared `D` before `try_create` takes the
            // context lock. `None` means no clock yet: wait like NoDataCreated.
            let offset = match self.resolve_clock_offset() {
                Ok(Some(offset)) => offset,
                Ok(None) => {
                    if mxl_helper::is_flushing(self) {
                        return Err(gst::FlowError::Flushing);
                    }
                    std::thread::sleep(Duration::from_millis(1));
                    continue;
                }
                Err(_) => {
                    gst::element_imp_error!(
                        self,
                        gst::CoreError::Failed,
                        ["Internal clock offset error"]
                    );
                    return Err(gst::FlowError::Error);
                }
            };
            match self.try_create(offset) {
                Ok(r) => match r {
                    CreateState::DataCreated(buffer) => {
                        return Ok(CreateSuccess::NewBuffer(buffer));
                    }
                    CreateState::NoDataCreated => {
                        // The producer has not committed the next grain yet. Only
                        // bail when the pipeline is tearing us down (basesrc calls
                        // unlock(), which sets is_flushing); otherwise keep waiting.
                        // Do not key this on current_state(): during PAUSED→PLAYING
                        // the streaming task can observe Paused before the producer
                        // commits its first grain, which would wrongly emit EOS and
                        // preroll the consumer empty.
                        if mxl_helper::is_flushing(self) {
                            return Err(gst::FlowError::Flushing);
                        }
                        std::thread::sleep(Duration::from_millis(1));
                    }
                },
                Err(e) => return Err(e),
            }
        }
    }
}

impl MxlSrc {
    /// Live latency to advertise: one grain period of the attached discrete flow.
    ///
    /// A grain becomes readable only once the producer has committed it, and this
    /// source delivers the most recently committed grain, so at any instant the
    /// reader may sit up to one grain period behind the live edge. One period is
    /// therefore the source latency downstream should budget. Returns `None` before
    /// the flow is attached (or for audio), leaving the BaseSrc default in place.
    fn live_latency(&self) -> Option<gst::ClockTime> {
        let context = self.context.lock().ok()?;
        let state = context.state.as_ref()?;
        match state.flow_state.as_ref()? {
            FlowState::Discrete(discrete) => Some(timing::index_period(&discrete.grain_rate)),
            FlowState::Continuous(_) => None,
        }
    }

    fn try_create(&self, offset: u64) -> Result<CreateState, gst::FlowError> {
        let mut context = self.context.lock().map_err(|_| gst::FlowError::Error)?;
        let state = context.state.as_mut().ok_or(gst::FlowError::Error)?;
        match &state.flow_state {
            Some(FlowState::Discrete(_)) => create_discrete(self, state, offset),
            Some(FlowState::Continuous(_)) => create_continuous(self, state, offset),
            None => Err(gst::FlowError::Error),
        }
    }
}
