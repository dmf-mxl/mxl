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
use gst_base::prelude::BaseSinkExt;
use gst_base::subclass::prelude::*;
use gstreamer as gst;
use gstreamer_audio as gst_audio;
use gstreamer_base as gst_base;

use mxl::MxlInstance;
use mxl::config::get_mxl_so_path;
use tracing::trace;

use std::sync::LazyLock;
use std::sync::Mutex;
use std::sync::MutexGuard;

use crate::clock::ClockOffsetExt;
use crate::mxlsink;
use crate::mxlsink::state::Context;
use crate::mxlsink::state::DEFAULT_DOMAIN;
use crate::mxlsink::state::DEFAULT_FLOW_ID;
use crate::mxlsink::state::FlowState;
use crate::mxlsink::state::Settings;
use crate::mxlsink::state::State;
use crate::mxlsink::state::init_state_with_audio;
use crate::mxlsink::state::init_state_with_data;
use crate::mxlsink::state::init_state_with_video;
use crate::mxlsink::{render_continuous, render_discrete};

pub(crate) static CAT: LazyLock<gst::DebugCategory> = LazyLock::new(|| {
    gst::DebugCategory::new("mxlsink", gst::DebugColorFlags::empty(), Some("MXL Sink"))
});

#[derive(Default)]
pub struct MxlSink {
    settings: Mutex<Settings>,
    context: Mutex<Context>,
    shared_clock_offset: Mutex<Option<crate::clock::SharedClockOffset>>,
}

#[glib::object_subclass]
impl ObjectSubclass for MxlSink {
    const NAME: &'static str = "GstRsMxlSink";
    type Type = mxlsink::MxlSink;
    type ParentType = gst_base::BaseSink;
}

impl ObjectImpl for MxlSink {
    fn properties() -> &'static [glib::ParamSpec] {
        static PROPERTIES: LazyLock<Vec<glib::ParamSpec>> = LazyLock::new(|| {
            vec![
                glib::ParamSpecString::builder("flow-id")
                    .nick("FlowID")
                    .blurb("Flow ID")
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
        self.parent_constructed();
        self.obj().set_sync(true);
    }

    fn set_property(&self, _id: usize, value: &glib::Value, pspec: &glib::ParamSpec) {
        if let Ok(mut settings) = self.settings.lock() {
            match pspec.name() {
                "flow-id" => {
                    if let Ok(flow_id) = value.get::<String>() {
                        gst::info!(
                            CAT,
                            imp = self,
                            "Changing flow-id from {} to {}",
                            settings.flow_id,
                            flow_id
                        );
                        settings.flow_id = flow_id;
                    } else {
                        gst::error!(CAT, imp = self, "Invalid type for flow-id property");
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
                "flow-id" => settings.flow_id.to_value(),
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

impl GstObjectImpl for MxlSink {}

impl ElementImpl for MxlSink {
    fn metadata() -> Option<&'static gst::subclass::ElementMetadata> {
        static ELEMENT_METADATA: LazyLock<gst::subclass::ElementMetadata> = LazyLock::new(|| {
            gst::subclass::ElementMetadata::new(
                "MXL Sink",
                "Sink/Video",
                "Generates an MXL flow from GStreamer buffers",
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
                    for ch in 1..64 {
                        let mask = gst::Bitmask::from((1u64 << ch) - 1);
                        caps.make_mut().append(
                            gst::Caps::builder("audio/x-raw")
                                .field("format", "F32LE")
                                .field("layout", "interleaved")
                                .field("channels", ch)
                                .field("channel-mask", mask)
                                .build(),
                        );
                    }
                    caps.make_mut().append(
                        gst::Caps::builder("meta/x-st-2038")
                            .field("alignment", "frame")
                            .build(),
                    );
                }

                let sink_pad_template = gst::PadTemplate::new(
                    "sink",
                    gst::PadDirection::Sink,
                    gst::PadPresence::Always,
                    &caps,
                )?;

                Ok(vec![sink_pad_template])
            });

        match PAD_TEMPLATES.as_ref() {
            Ok(templates) => templates,
            Err(err) => {
                trace!("Failed to create pad templates: {:?}", err);
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

impl crate::clock::ClockOffsetExt for MxlSink {
    fn mxl_now(&self) -> Result<Option<u64>, crate::clock::ClockOffsetError> {
        let context = self
            .context
            .lock()
            .map_err(|_| crate::clock::ClockOffsetError::Failed)?;
        Ok(context
            .state
            .as_ref()
            .map(|state| state.instance.get_time()))
    }

    fn shared_clock_offset(&self) -> &Mutex<Option<crate::clock::SharedClockOffset>> {
        &self.shared_clock_offset
    }
}

impl BaseSinkImpl for MxlSink {
    fn start(&self) -> Result<(), gst::ErrorMessage> {
        // Adopt the pipeline-shared offset cell now, during the sequential
        // READY->PAUSED state change, so sibling MXL elements deterministically
        // share one `D` (see clock.rs). Establishing it lazily at first render
        // races: two sinks can each publish their own cell and sample `D`
        // microseconds apart, rounding the same PTS to indices one grain apart
        // and mispairing flows in st2038combiner.
        self.ensure_clock_offset()
            .map_err(|_| crate::clock::ClockOffsetError::Failed.into_error_message())?;

        let mut context = self.context.lock().map_err(|e| {
            gst::error_msg!(gst::CoreError::Failed, ["Failed to get state mutex: {}", e])
        })?;
        let settings = self.settings.lock().map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to get settings mutex: {}", e]
            )
        })?;
        let instance = init_mxl_instance(&settings)?;
        context.state = Some(State {
            instance,
            flow_config: None,
            flow_state: None,
        });

        Ok(())
    }

    fn stop(&self) -> Result<(), gst::ErrorMessage> {
        let mut context = self.context.lock().map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to get context mutex: {}", e]
            )
        })?;

        // Destroy the flow writers before dropping the MXL instance they belong
        // to, then release the instance and clock.
        if let Some(mut state) = context.state.take() {
            match state.flow_state.take() {
                Some(FlowState::Discrete(discrete)) => {
                    discrete.writer.destroy().map_err(|e| {
                        gst::error_msg!(
                            gst::CoreError::Failed,
                            ["Failed to destroy discrete writer: {}", e]
                        )
                    })?;
                }
                Some(FlowState::Continuous(continuous)) => {
                    continuous.writer.destroy().map_err(|e| {
                        gst::error_msg!(
                            gst::CoreError::Failed,
                            ["Failed to destroy continuous writer: {}", e]
                        )
                    })?;
                }
                None => {}
            }
        }

        gst::info!(CAT, imp = self, "Stopped");
        Ok(())
    }

    fn render(&self, buffer: &gst::Buffer) -> Result<gst::FlowSuccess, gst::FlowError> {
        trace!("START RENDER");

        // Establish the pipeline-shared `D` before taking the context lock.
        let offset = match self.resolve_clock_offset() {
            Ok(Some(offset)) => offset,
            Ok(None) => {
                gst::element_imp_error!(
                    self,
                    gst::CoreError::Failed,
                    ["Pipeline clock not available for clock offset"]
                );
                return Err(gst::FlowError::Error);
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

        let mut context = self.context.lock().map_err(|_| gst::FlowError::Error)?;
        let state = context.state.as_mut().ok_or(gst::FlowError::Error)?;
        // Borrow the element for the duration of this render call so
        // the format-specific paths can read its base time via
        // `Element::base_time()` without `State` having to cache a
        // strong ref (which would form a refcount cycle).
        let element = self.obj();
        let element: &gst::Element = element.upcast_ref();
        match &state.flow_state {
            Some(FlowState::Discrete(_)) => {
                render_discrete::discrete(state, element, buffer, offset)
            }
            Some(FlowState::Continuous(_)) => {
                render_continuous::continuous(state, element, buffer, offset)
            }
            None => Err(gst::FlowError::Error),
        }
    }

    fn prepare(&self, buffer: &gst::Buffer) -> Result<gst::FlowSuccess, gst::FlowError> {
        self.parent_prepare(buffer)
    }

    fn render_list(&self, list: &gst::BufferList) -> Result<gst::FlowSuccess, gst::FlowError> {
        self.parent_render_list(list)
    }

    fn prepare_list(&self, list: &gst::BufferList) -> Result<gst::FlowSuccess, gst::FlowError> {
        self.parent_prepare_list(list)
    }

    fn query(&self, query: &mut gst::QueryRef) -> bool {
        BaseSinkImplExt::parent_query(self, query)
    }

    fn event(&self, event: gst::Event) -> bool {
        self.parent_event(event)
    }

    fn caps(&self, filter: Option<&gst::Caps>) -> Option<gst::Caps> {
        self.parent_caps(filter)
    }

    fn set_caps(&self, caps: &gst::Caps) -> Result<(), gst::LoggableError> {
        let mut context = self
            .context
            .lock()
            .map_err(|e| gst::loggable_error!(CAT, "Failed to lock context mutex: {}", e))?;
        let state = context
            .state
            .as_mut()
            .ok_or(gst::loggable_error!(CAT, "Failed to get state",))?;

        let settings = self
            .settings
            .lock()
            .map_err(|e| gst::loggable_error!(CAT, "Failed to lock settings mutex: {}", e))?;

        let structure = caps
            .structure(0)
            .ok_or_else(|| gst::loggable_error!(CAT, "No structure in caps {}", caps))?;
        let name = structure.name();
        if name == "video/x-raw" {
            init_state_with_video(state, structure, &settings.flow_id)?;
            Ok(())
        } else if name == "audio/x-raw" {
            let info = gst_audio::AudioInfo::from_caps(caps)
                .map_err(|e| gst::loggable_error!(CAT, "Invalid audio caps: {}", e))?;

            init_state_with_audio(state, info, &settings.flow_id)?;
            Ok(())
        } else if name == "meta/x-st-2038" {
            init_state_with_data(state, structure, &settings.flow_id)?;
            Ok(())
        } else {
            Err(gst::loggable_error!(CAT, "Unknown caps: {}", caps))
        }
    }

    fn fixate(&self, caps: gst::Caps) -> gst::Caps {
        self.parent_fixate(caps)
    }

    fn propose_allocation(
        &self,
        query: &mut gst::query::Allocation,
    ) -> Result<(), gst::LoggableError> {
        self.parent_propose_allocation(query)
    }
}

fn init_mxl_instance(
    settings: &MutexGuard<'_, Settings>,
) -> Result<MxlInstance, gst::ErrorMessage> {
    let mxl_api = mxl::load_api(get_mxl_so_path())
        .map_err(|e| gst::error_msg!(gst::CoreError::Failed, ["Failed to load MXL API: {}", e]))?;

    let mxl_instance =
        mxl::MxlInstance::new(mxl_api, settings.domain.as_str(), "").map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to load MXL instance: {}", e]
            )
        })?;

    // Best-effort: reclaim any flow directories left behind by a writer that
    // exited or crashed before its destructors ran. Long-running processes
    // get a fresh GC pass every time an element opens an instance.
    if let Err(e) = mxl_instance.garbage_collect_flows() {
        gst::warning!(CAT, "MXL garbage collection on init failed: {}", e);
    }

    Ok(mxl_instance)
}
