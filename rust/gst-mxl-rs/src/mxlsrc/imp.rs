//! MXL Source Implementation
//!
//! This module contains the core implementation of the mxlsrc GStreamer element.
//! It implements GStreamer's PushSrc trait (a type of BaseSrc), handling:
//! - Element lifecycle (start/stop state transitions)
//! - Property management (video-flow-id, audio-flow-id, domain)
//! - Caps negotiation (reading flow definition, setting caps)
//! - Buffer creation (calling create_audio or create_video on-demand)
//! - Live mode operation (timestamps based on running time)
//!
//! ## GStreamer PushSrc Overview (for non-GStreamer developers)
//! PushSrc is a base class for source elements that:
//! - Produce buffers on-demand via the `create()` method
//! - Operate in either live or non-live mode
//! - Handle scheduling and pushing buffers downstream
//! - Support seeking (if non-live) and querying
//!
//! ## Implementation Structure
//! - `MxlSrc`: The struct holding element state (settings, context, clock_wait)
//! - `ObjectImpl`: GObject property system integration
//! - `ElementImpl`: GStreamer element metadata and pad templates
//! - `BaseSrcImpl`: Source-specific behavior (start, stop, negotiate, set_caps)
//! - `PushSrcImpl`: On-demand buffer creation via create()

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
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

use crate::mxlsrc;
use crate::mxlsrc::create_audio::create_audio;
use crate::mxlsrc::create_video::create_video;
use crate::mxlsrc::mxl_helper;
use crate::mxlsrc::state::Context;
use crate::mxlsrc::state::DEFAULT_DOMAIN;
use crate::mxlsrc::state::DEFAULT_FLOW_ID;
use crate::mxlsrc::state::Settings;

/// GStreamer debug category for logging mxlsrc-specific messages
pub(crate) static CAT: LazyLock<gst::DebugCategory> = LazyLock::new(|| {
    gst::DebugCategory::new("mxlsrc", gst::DebugColorFlags::empty(), Some("MXL Source"))
});

/// Clock waiting state for handling pipeline synchronization
struct ClockWait {
    /// Active clock wait (if any) for cancellation
    clock_id: Option<gst::SingleShotClockId>,

    /// True when flushing (cancels clock waits)
    flushing: bool,
}

impl Default for ClockWait {
    fn default() -> ClockWait {
        ClockWait {
            clock_id: None,
            flushing: true,  // Start in flushing state until unlock_stop()
        }
    }
}

/// MXL Source element implementation.
///
/// This struct holds all mutable state for the element. Fields are wrapped
/// in Mutex for thread safety (GStreamer may call methods from multiple threads).
#[derive(Default)]
pub struct MxlSrc {
    /// User-configurable properties (video/audio flow IDs, domain)
    pub settings: Mutex<Settings>,

    /// Runtime state (MXL instance, reader, flow config)
    pub context: Mutex<Context>,

    /// Clock synchronization state
    clock_wait: Mutex<ClockWait>,
}

/// Result of attempting to create a buffer.
///
/// `NoDataCreated` signals that the flow is stale and should be re-initialized.
pub enum CreateState {
    /// Buffer was created successfully
    DataCreated(Buffer),

    /// No data available (flow stale, needs re-initialization)
    NoDataCreated,
}

/// Registers this type as a GLib object subclass
#[glib::object_subclass]
impl ObjectSubclass for MxlSrc {
    /// Internal type name (must be unique)
    const NAME: &'static str = "GstRsMxlSrc";

    /// Public wrapper type
    type Type = mxlsrc::MxlSrc;

    /// Parent class (PushSrc provides on-demand buffer creation)
    type ParentType = gst_base::PushSrc;
}

/// GObject property system implementation
impl ObjectImpl for MxlSrc {
    /// Returns the list of properties this element exposes
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

    /// Called when the element is constructed.
    ///
    /// Configures the source as live (real-time) with time-based format.
    fn constructed(&self) {
        self.parent_constructed();

        // Initialize tracing (debug/diagnostics feature)
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

        // Configure as live source (timestamps based on running time, not file position)
        obj.set_live(true);

        // Use time format (not bytes or other formats)
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

/// GStreamer object implementation (inherits from GstObject)
impl GstObjectImpl for MxlSrc {}

/// GStreamer element implementation (metadata and pads)
impl ElementImpl for MxlSrc {
    /// Returns element metadata displayed by gst-inspect
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
}

/// BaseSrc implementation (source-specific behavior)
impl BaseSrcImpl for MxlSrc {
    /// Handles events from downstream (seek, reconfigure, etc.)
    fn event(&self, event: &gst::Event) -> bool {
        self.parent_event(event)
    }

    /// Negotiates caps with downstream.
    ///
    /// Reads the MXL flow definition (JSON), parses it into video or audio
    /// parameters, and proposes caps to downstream.
    ///
    /// # Returns
    /// * `Ok(())` if caps were negotiated successfully
    /// * `Err(LoggableError)` if flow ID not set, MXL query failed, or caps invalid
    fn negotiate(&self) -> Result<(), gst::LoggableError> {
        gst::info!(CAT, imp = self, "Negotiating caps…");

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
        if settings.audio_flow.is_some() && settings.video_flow.is_some() {
            gst::warning!(CAT, imp = self, "You can't set both video and audio flows");
            return self.parent_negotiate();
        }
        if settings.domain.is_empty()
            || settings.video_flow.is_none() && settings.audio_flow.is_none()
        {
            gst::warning!(CAT, imp = self, "domain or flow-id not set yet");
            return self.parent_negotiate();
        }
        let flow_id = mxl_helper::get_flow_type_id(&settings)?;
        let json_flow_description = mxl_helper::get_mxl_flow_json(instance, flow_id)?;
        let flow_description = mxl_helper::get_flow_def(self, json_flow_description)?;
        mxl_helper::set_json_caps(self, flow_description)
    }

    /// Called when caps are finalized (after negotiation succeeds).
    ///
    /// Verifies the negotiated caps match what we expected from the flow definition.
    ///
    /// # Arguments
    /// * `caps` - Negotiated capabilities (video/x-raw or audio/x-raw)
    ///
    /// # Returns
    /// * `Ok(())` if caps are valid
    /// * `Err(LoggableError)` if caps are missing required fields
    fn set_caps(&self, caps: &gst::Caps) -> Result<(), gst::LoggableError> {
        let structure = caps
            .structure(0)
            .ok_or_else(|| gst::loggable_error!(CAT, "No structure in caps {}", caps))?;

        let format = structure
            .get::<String>("format")
            .map_err(|e| gst::loggable_error!(CAT, "Failed to set caps {}", e))?;

        if format == "v210" {
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
        } else if format == "F32LE" {
            let rate = structure
                .get::<i32>("rate")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to get rate from caps: {}", e))?;

            let channels = structure.get::<i32>("channels").map_err(|e| {
                gst::loggable_error!(CAT, "Failed to get channels from caps: {}", e)
            })?;

            let format = structure
                .get::<String>("format")
                .map_err(|e| gst::loggable_error!(CAT, "Failed to get format from caps: {}", e))?;
            trace!(
                "Negotiated caps: format={}, rate={}, channel_count={} ",
                format, rate, channels
            );

            Ok(())
        } else {
            Err(gst::loggable_error!(
                CAT,
                "Failed to set caps: No valid format"
            ))
        }
    }

    /// Called when transitioning to PAUSED state (start the source).
    ///
    /// Initializes the MXL instance, creates flow readers, and prepares
    /// for buffer creation.
    ///
    /// # Returns
    /// * `Ok(())` if MXL initialization succeeded
    /// * `Err(ErrorMessage)` if initialization failed
    fn start(&self) -> Result<(), gst::ErrorMessage> {
        // Clear flushing flag
        self.unlock_stop()?;

        // Initialize MXL readers
        mxl_helper::init(self)?;
        gst::info!(CAT, imp = self, "Started");

        Ok(())
    }

    /// Called when transitioning to NULL state (stop the source).
    ///
    /// Destroys MXL readers and releases shared memory resources.
    ///
    /// # Returns
    /// * `Ok(())` if cleanup succeeded
    /// * `Err(ErrorMessage)` if cleanup failed
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

/// PushSrc implementation (on-demand buffer creation)
impl PushSrcImpl for MxlSrc {
    /// Creates a buffer on-demand.
    ///
    /// Called by the BaseSrc base class when downstream requests data.
    /// Loops until a buffer is created or EOS is reached.
    ///
    /// # Arguments
    /// * `_buffer` - Optional pre-allocated buffer (unused, we always create new)
    ///
    /// # Returns
    /// * `Ok(CreateSuccess::NewBuffer(buffer))` if a buffer was created
    /// * `Err(FlowError::Eos)` if the element is stopping
    /// * `Err(FlowError)` if buffer creation failed
    ///
    /// # Flow Staleness
    /// If try_create() returns NoDataCreated, the flow is stale (writer died).
    /// We re-initialize the reader and retry unless we're stopping.
    fn create(
        &self,
        _buffer: Option<&mut gst::BufferRef>,
    ) -> Result<CreateSuccess, gst::FlowError> {
        loop {
            match self.try_create() {
                Ok(r) => match r {
                    CreateState::DataCreated(buffer) => {
                        return Ok(CreateSuccess::NewBuffer(buffer));
                    }
                    CreateState::NoDataCreated => {
                        // Check if we're stopping (EOS)
                        if self.obj().current_state() == gst::State::Paused
                            || self.obj().current_state() == gst::State::Null
                        {
                            return Err(gst::FlowError::Eos);
                        };

                        // Flow is stale: re-initialize reader and retry
                        let _ = mxl_helper::init(self);
                    }
                },
                Err(e) => return Err(e),
            }
        }
    }
}

impl MxlSrc {
    /// Attempts to create a buffer without retrying.
    ///
    /// Routes to create_video or create_audio based on negotiated caps.
    fn try_create(&self) -> Result<CreateState, gst::FlowError> {
        let mut context = self.context.lock().map_err(|_| gst::FlowError::Error)?;
        let state = context.state.as_mut().ok_or(gst::FlowError::Error)?;
        if state.video.is_some() {
            create_video(self, state)
        } else if state.audio.is_some() {
            create_audio(self, state)
        } else {
            Err(gst::FlowError::Error)
        }
    }
}
