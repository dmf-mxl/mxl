//! MXL Sink Implementation
//!
//! This module contains the core implementation of the mxlsink GStreamer element.
//! It implements GStreamer's BaseSink trait, handling:
//! - Element lifecycle (start/stop state transitions)
//! - Property management (flow-id, domain)
//! - Caps negotiation (determining audio vs. video format)
//! - Buffer rendering (calling render_audio or render_video)
//! - Clock synchronization (aligning with pipeline timing)
//!
//! ## GStreamer BaseSink Overview (for non-GStreamer developers)
//! BaseSink is a base class for sink elements that:
//! - Receives buffers from upstream via the `render()` method
//! - Handles synchronization by default (waits until buffer PTS before rendering)
//! - Manages state transitions (NULL -> READY -> PAUSED -> PLAYING)
//! - Provides hooks for capabilities negotiation (`set_caps()`)
//!
//! ## Implementation Structure
//! - `MxlSink`: The struct holding element state (settings, context, clock_wait)
//! - `ObjectImpl`: GObject property system integration
//! - `ElementImpl`: GStreamer element metadata and pad templates
//! - `BaseSinkImpl`: Sink-specific behavior (start, stop, render, set_caps)

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

use crate::mxlsink;
use crate::mxlsink::state::Context;
use crate::mxlsink::state::DEFAULT_DOMAIN;
use crate::mxlsink::state::DEFAULT_FLOW_ID;
use crate::mxlsink::state::Settings;
use crate::mxlsink::state::State;
use crate::mxlsink::state::init_state_with_audio;
use crate::mxlsink::state::init_state_with_video;
use crate::mxlsink::{render_audio, render_video};

/// GStreamer debug category for logging mxlsink-specific messages.
///
/// Used with gst::info!, gst::warning!, gst::error! macros.
/// Set GST_DEBUG=mxlsink:5 to see TRACE-level logs.
pub(crate) static CAT: LazyLock<gst::DebugCategory> = LazyLock::new(|| {
    gst::DebugCategory::new("mxlsink", gst::DebugColorFlags::empty(), Some("MXL Sink"))
});

/// Clock waiting state for handling pipeline synchronization.
///
/// GStreamer sinks can wait on the pipeline clock before rendering buffers.
/// This struct holds the clock ID for cancellation during flushing/stopping.
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

/// MXL Sink element implementation.
///
/// This struct holds all mutable state for the element. Each field is wrapped
/// in a Mutex because GStreamer may call methods from multiple threads.
///
/// ## Thread Safety
/// GStreamer elements must be Send + Sync. Rust's type system enforces this
/// by requiring Mutex for interior mutability.
#[derive(Default)]
pub struct MxlSink {
    /// User-configurable properties (flow-id, domain)
    settings: Mutex<Settings>,

    /// Runtime state (MXL instance, writer, flow config)
    context: Mutex<Context>,

    /// Clock synchronization state
    clock_wait: Mutex<ClockWait>,
}

/// Registers this type as a GLib object subclass.
///
/// This macro generates the boilerplate for GStreamer's object system,
/// allowing the element to be instantiated via gst_element_factory_make().
#[glib::object_subclass]
impl ObjectSubclass for MxlSink {
    /// Internal type name (must be unique across all GStreamer elements)
    const NAME: &'static str = "GstRsMxlSink";

    /// Public wrapper type
    type Type = mxlsink::MxlSink;

    /// Parent class (BaseSink provides sink behavior)
    type ParentType = gst_base::BaseSink;
}

/// GObject property system implementation.
///
/// Defines properties that can be set via g_object_set() or gst-launch's
/// property syntax (e.g., mxlsink flow-id=<uuid>).
impl ObjectImpl for MxlSink {
    /// Returns the list of properties this element exposes.
    ///
    /// Properties are lazily initialized once and cached for performance.
    fn properties() -> &'static [glib::ParamSpec] {
        static PROPERTIES: LazyLock<Vec<glib::ParamSpec>> = LazyLock::new(|| {
            vec![
                // UUID of the MXL flow (must be set before PLAYING state)
                glib::ParamSpecString::builder("flow-id")
                    .nick("FlowID")
                    .blurb("Flow ID")
                    .default_value(DEFAULT_FLOW_ID)
                    .mutable_ready()  // Can only change in READY state or below
                    .build(),

                // Shared memory domain path (e.g., "/dev/shm")
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

    /// Called when the element is constructed (after allocation).
    ///
    /// Sets up tracing (if enabled) and configures the sink to synchronize
    /// buffers with the pipeline clock.
    fn constructed(&self) {
        // Initialize tracing (debug/diagnostics feature)
        #[cfg(feature = "tracing")]
        {
            use tracing_subscriber::filter::LevelFilter;
            use tracing_subscriber::util::SubscriberInitExt;

            // Configure console logging with file/line/thread info
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

        // Call parent class constructor
        self.parent_constructed();

        // Enable synchronization (buffers wait for PTS before rendering)
        self.obj().set_sync(true);
    }

    /// Called when a property is set via g_object_set() or gst-launch.
    ///
    /// # Arguments
    /// * `_id` - Property index (unused; we match by name)
    /// * `value` - New value for the property
    /// * `pspec` - Property specification (contains name)
    fn set_property(&self, _id: usize, value: &glib::Value, pspec: &glib::ParamSpec) {
        // Acquire settings lock (fails if mutex is poisoned)
        if let Ok(mut settings) = self.settings.lock() {
            match pspec.name() {
                "flow-id" => {
                    // Extract string value from GValue
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
            // Mutex poisoned (panic occurred while locked)
            gst::error!(
                CAT,
                imp = self,
                "Settings mutex poisoned — property change ignored"
            );
        }
    }

    /// Called when a property is retrieved via g_object_get().
    ///
    /// # Arguments
    /// * `_id` - Property index (unused)
    /// * `pspec` - Property specification (contains name)
    ///
    /// # Returns
    /// GValue containing the property value
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

/// GStreamer object implementation (inherits from GstObject).
///
/// No custom behavior needed; uses default implementation.
impl GstObjectImpl for MxlSink {}

/// GStreamer element implementation.
///
/// Defines element metadata (name, description, author) and pad templates
/// (what media formats the element can accept).
impl ElementImpl for MxlSink {
    /// Returns element metadata displayed by gst-inspect.
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

    /// Returns pad templates (defines accepted media formats).
    ///
    /// This element has one sink pad accepting:
    /// - video/x-raw with format=v210
    /// - audio/x-raw with format=F32LE, layout=interleaved, channels=1-63
    ///
    /// ## GStreamer Pads (for non-GStreamer developers)
    /// - **Pad**: A connection point on an element (source or sink)
    /// - **Pad Template**: Defines the formats a pad can handle
    /// - **Caps**: Concrete format description (resolution, framerate, etc.)
    fn pad_templates() -> &'static [gst::PadTemplate] {
        static PAD_TEMPLATES: LazyLock<Result<Vec<gst::PadTemplate>, glib::BoolError>> =
            LazyLock::new(|| {
                let mut caps = gst::Caps::new_empty();
                {
                    let caps_mut = caps.make_mut();

                    // Add video caps (v210 format only)
                    caps_mut.append(
                        gst::Caps::builder("video/x-raw")
                            .field("format", "v210")
                            .build(),
                    );

                    // Add audio caps for 1-63 channels (generate channel masks)
                    for ch in 1..64 {
                        // Generate bitmask for channel count (e.g., 0x03 for 2 channels)
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
                }

                // Create sink pad template (accepts video or audio)
                let sink_pad_template = gst::PadTemplate::new(
                    "sink",
                    gst::PadDirection::Sink,
                    gst::PadPresence::Always,  // Pad is always present (not request/sometimes)
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

    /// Handles state transitions (NULL->READY->PAUSED->PLAYING).
    ///
    /// Uses default implementation (no custom behavior needed).
    fn change_state(
        &self,
        transition: gst::StateChange,
    ) -> Result<gst::StateChangeSuccess, gst::StateChangeError> {
        self.parent_change_state(transition)
    }
}

/// BaseSink implementation (sink-specific behavior).
///
/// This is where the core logic lives: starting/stopping the element,
/// rendering buffers, and handling caps negotiation.
impl BaseSinkImpl for MxlSink {
    /// Called when transitioning to PAUSED state (start the sink).
    ///
    /// Initializes the MXL instance but doesn't create the flow yet
    /// (that happens in set_caps after negotiation).
    ///
    /// # Returns
    /// * `Ok(())` if MXL initialized successfully
    /// * `Err(ErrorMessage)` if initialization failed
    fn start(&self) -> Result<(), gst::ErrorMessage> {
        // Acquire context lock
        let mut context = self.context.lock().map_err(|e| {
            gst::error_msg!(gst::CoreError::Failed, ["Failed to get state mutex: {}", e])
        })?;

        // Clear flushing flag (allow buffer processing)
        self.unlock_stop()?;

        // Get settings (flow-id, domain)
        let settings = self.settings.lock().map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to get settings mutex: {}", e]
            )
        })?;

        // Initialize MXL instance (loads shared library, opens domain)
        let instance = init_mxl_instance(&settings)?;

        // Create empty state (flow will be created in set_caps)
        context.state = Some(State {
            instance,
            flow: None,
            initial_time: None,
            video: None,
            audio: None,
        });

        Ok(())
    }

    /// Called when transitioning to NULL state (stop the sink).
    ///
    /// Destroys MXL writers and releases shared memory resources.
    ///
    /// # Returns
    /// * `Ok(())` if cleanup succeeded
    /// * `Err(ErrorMessage)` if cleanup failed
    fn stop(&self) -> Result<(), gst::ErrorMessage> {
        // Acquire context lock
        let mut context = self.context.lock().map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to get context mutex: {}", e]
            )
        })?;

        // Set flushing flag (cancels any pending clock waits)
        self.unlock()?;

        // Get state
        let state = context.state.as_mut().ok_or(gst::error_msg!(
            gst::CoreError::Failed,
            ["Failed to get state"]
        ))?;

        // Destroy audio writer if present
        if state.audio.is_some() {
            state
                .audio
                .take()
                .ok_or(gst::error_msg!(
                    gst::CoreError::Failed,
                    ["Failed to get audio state"]
                ))?
                .writer
                .destroy()
                .map_err(|_| gst::error_msg!(gst::CoreError::Failed, ["Failed to destroy flow"]))?
        };

        // Destroy video writer if present
        if state.video.is_some() {
            state
                .video
                .take()
                .ok_or(gst::error_msg!(
                    gst::CoreError::Failed,
                    ["Failed to get video state"]
                ))?
                .writer
                .destroy()
                .map_err(|_| gst::error_msg!(gst::CoreError::Failed, ["Failed to destroy flow"]))?
        }

        gst::info!(CAT, imp = self, "Stopped");
        Ok(())
    }

    /// Called to render each buffer received from upstream.
    ///
    /// This is the main data path. BaseSink handles synchronization
    /// (waiting until buffer PTS), then calls this method to actually
    /// write the buffer to MXL.
    ///
    /// # Arguments
    /// * `buffer` - GStreamer buffer with video or audio data
    ///
    /// # Returns
    /// * `Ok(FlowSuccess::Ok)` if buffer was rendered successfully
    /// * `Err(FlowError)` if rendering failed
    ///
    /// # Routing
    /// Delegates to render_video or render_audio based on negotiated caps.
    fn render(&self, buffer: &gst::Buffer) -> Result<gst::FlowSuccess, gst::FlowError> {
        trace!("START RENDER");

        // Acquire context lock
        let mut context = self.context.lock().map_err(|_| gst::FlowError::Error)?;
        let state = context.state.as_mut().ok_or(gst::FlowError::Error)?;

        // Route to appropriate renderer based on media type
        if state.video.is_some() {
            render_video::video(self, state, buffer)
        } else {
            render_audio::audio(state, buffer)
        }
    }

    /// Called before rendering a buffer (optional pre-processing hook).
    ///
    /// Uses default implementation (no custom behavior).
    fn prepare(&self, buffer: &gst::Buffer) -> Result<gst::FlowSuccess, gst::FlowError> {
        self.parent_prepare(buffer)
    }

    /// Called to render a list of buffers (batch rendering).
    ///
    /// Uses default implementation (renders each buffer individually).
    fn render_list(&self, list: &gst::BufferList) -> Result<gst::FlowSuccess, gst::FlowError> {
        self.parent_render_list(list)
    }

    /// Called before rendering a buffer list (pre-processing hook).
    ///
    /// Uses default implementation.
    fn prepare_list(&self, list: &gst::BufferList) -> Result<gst::FlowSuccess, gst::FlowError> {
        self.parent_prepare_list(list)
    }

    /// Handles queries from other elements (duration, position, etc.).
    ///
    /// Uses default implementation.
    fn query(&self, query: &mut gst::QueryRef) -> bool {
        BaseSinkImplExt::parent_query(self, query)
    }

    /// Handles events from upstream (EOS, FLUSH, SEGMENT, etc.).
    ///
    /// Uses default implementation.
    fn event(&self, event: gst::Event) -> bool {
        self.parent_event(event)
    }

    /// Returns caps the sink can accept (with optional filter).
    ///
    /// Uses default implementation (returns pad template caps).
    fn caps(&self, filter: Option<&gst::Caps>) -> Option<gst::Caps> {
        self.parent_caps(filter)
    }

    /// Called when caps are negotiated (format is finalized).
    ///
    /// This is where the MXL flow is created based on the negotiated format.
    /// Extracts format details from caps and calls init_state_with_video or
    /// init_state_with_audio.
    ///
    /// # Arguments
    /// * `caps` - Negotiated capabilities (video/x-raw or audio/x-raw)
    ///
    /// # Returns
    /// * `Ok(())` if flow was created successfully
    /// * `Err(LoggableError)` if caps are invalid or flow creation failed
    ///
    /// # GStreamer Caps Negotiation (for non-GStreamer developers)
    /// 1. Upstream proposes caps (e.g., "video/x-raw, width=1920, height=1080")
    /// 2. Sink checks if it can accept those caps (via pad template)
    /// 3. If compatible, caps are "negotiated" and set_caps() is called
    /// 4. Element configures itself for that specific format
    fn set_caps(&self, caps: &gst::Caps) -> Result<(), gst::LoggableError> {
        // Acquire locks
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

        // Extract format from caps
        let structure = caps
            .structure(0)
            .ok_or_else(|| gst::loggable_error!(CAT, "No structure in caps {}", caps))?;
        let name = structure.name();

        // Initialize state based on media type
        if name == "audio/x-raw" {
            // Parse audio caps into AudioInfo
            let info = gst_audio::AudioInfo::from_caps(caps)
                .map_err(|e| gst::loggable_error!(CAT, "Invalid audio caps: {}", e))?;

            // Create MXL audio flow
            init_state_with_audio(state, info, &settings.flow_id)?;
            Ok(())
        } else {
            // Create MXL video flow
            init_state_with_video(state, structure, &settings.flow_id)
        }
    }

    /// Called to refine caps during negotiation (pick specific values).
    ///
    /// Uses default implementation (picks first structure, first values).
    fn fixate(&self, caps: gst::Caps) -> gst::Caps {
        self.parent_fixate(caps)
    }

    /// Called to interrupt blocking operations (flush, stop, etc.).
    ///
    /// Cancels any pending clock waits and sets flushing flag.
    ///
    /// # Returns
    /// * `Ok(())` if unlock succeeded
    /// * `Err(ErrorMessage)` if mutex was poisoned
    fn unlock(&self) -> Result<(), gst::ErrorMessage> {
        gst::debug!(CAT, imp = self, "Unlocking");

        // Acquire clock wait lock
        let mut clock_wait = self.clock_wait.lock().map_err(|e| {
            gst::error_msg!(gst::CoreError::Failed, ["Failed to lock clock: {}", e])
        })?;

        // Cancel any pending clock wait
        if let Some(clock_id) = clock_wait.clock_id.take() {
            clock_id.unschedule();
        }

        // Set flushing flag (prevents new waits)
        clock_wait.flushing = true;

        Ok(())
    }

    /// Called to resume operations after unlock() (e.g., after flush-stop).
    ///
    /// Clears the flushing flag, allowing buffer processing to continue.
    ///
    /// # Returns
    /// * `Ok(())` if unlock_stop succeeded
    /// * `Err(ErrorMessage)` if mutex was poisoned
    fn unlock_stop(&self) -> Result<(), gst::ErrorMessage> {
        gst::debug!(CAT, imp = self, "Unlock stop");

        // Acquire clock wait lock
        let mut clock_wait = self.clock_wait.lock().map_err(|e| {
            gst::error_msg!(gst::CoreError::Failed, ["Failed to lock clock: {}", e])
        })?;

        // Clear flushing flag (allow buffer processing)
        clock_wait.flushing = false;

        Ok(())
    }

    /// Called to propose buffer allocation parameters to upstream.
    ///
    /// Uses default implementation (no special allocation requirements).
    fn propose_allocation(
        &self,
        query: &mut gst::query::Allocation,
    ) -> Result<(), gst::LoggableError> {
        self.parent_propose_allocation(query)
    }
}

/// Initializes the MXL instance from settings.
///
/// Loads the MXL shared library and creates an instance connected to the
/// specified domain (shared memory path).
///
/// # Arguments
/// * `settings` - Element settings (domain path)
///
/// # Returns
/// * `Ok(MxlInstance)` if initialization succeeded
/// * `Err(ErrorMessage)` if MXL library loading or instance creation failed
fn init_mxl_instance(
    settings: &MutexGuard<'_, Settings>,
) -> Result<MxlInstance, gst::ErrorMessage> {
    // Load MXL shared library (libmxl.so or mxl.dll)
    let mxl_api = mxl::load_api(get_mxl_so_path())
        .map_err(|e| gst::error_msg!(gst::CoreError::Failed, ["Failed to load MXL API: {}", e]))?;

    // Create MXL instance connected to the specified domain
    let mxl_instance =
        mxl::MxlInstance::new(mxl_api, settings.domain.as_str(), "").map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to load MXL instance: {}", e]
            )
        })?;

    Ok(mxl_instance)
}
