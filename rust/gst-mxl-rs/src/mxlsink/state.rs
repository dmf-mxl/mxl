//! State Management and Initialization for MXL Sink
//!
//! This module defines the runtime state structures used by mxlsink and provides
//! initialization functions that configure MXL flows based on GStreamer caps
//! (capabilities/format descriptions).
//!
//! ## Key Types
//! - `Settings`: User-configurable properties (flow ID, domain)
//! - `State`: Runtime state (MXL instance, writer, flow configuration)
//! - `VideoState`: Video-specific state (grain writer, frame counters, rate)
//! - `AudioState`: Audio-specific state (samples writer, batch size, bit depth)
//!
//! ## Initialization Flow
//! 1. GStreamer negotiates caps with the sink
//! 2. `set_caps()` calls `init_state_with_video()` or `init_state_with_audio()`
//! 3. These functions create MXL flow definitions (JSON) from caps
//! 4. MXL SDK creates or attaches to the flow in shared memory
//! 5. A writer handle is stored in state for rendering buffers

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{collections::HashMap, process, str::FromStr};

use crate::mxlsink::imp::CAT;
use gst::{ClockTime, StructureRef};
use gst_audio::AudioInfo;
use gstreamer as gst;
use gstreamer_audio as gst_audio;
use mxl::{
    FlowConfigInfo, GrainWriter, MxlInstance, Rational, SamplesWriter,
    flowdef::{
        Component, FlowDef, FlowDefAudio, FlowDefDetails, FlowDefVideo, InterlaceMode, Rate,
    },
};
use tracing::trace;

use uuid::Uuid;

/// Default value for the flow-id property (empty = must be set by user)
pub(crate) const DEFAULT_FLOW_ID: &str = "";

/// Default value for the domain property (empty = must be set by user)
pub(crate) const DEFAULT_DOMAIN: &str = "";

/// User-configurable settings for the mxlsink element.
///
/// These are set via GObject properties before the element enters the PLAYING state.
#[derive(Debug, Clone)]
pub(crate) struct Settings {
    /// UUID of the MXL flow to write to (creates if doesn't exist)
    pub flow_id: String,

    /// Shared memory domain path (e.g., "/dev/shm" on Linux)
    pub domain: String,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            flow_id: DEFAULT_FLOW_ID.to_owned(),
            domain: DEFAULT_DOMAIN.to_owned(),
        }
    }
}

/// Runtime state for the mxlsink element.
///
/// Holds the MXL instance, flow configuration, and format-specific state.
/// Only one of `video` or `audio` will be Some(_) depending on negotiated caps.
pub(crate) struct State {
    /// MXL SDK instance (handles shared memory communication)
    pub instance: MxlInstance,

    /// MXL flow configuration (ring buffer size, rate, etc.)
    pub flow: Option<FlowConfigInfo>,

    /// Video-specific state (present if caps are video/x-raw)
    pub video: Option<VideoState>,

    /// Audio-specific state (present if caps are audio/x-raw)
    pub audio: Option<AudioState>,

    /// Timestamp offset for synchronizing GStreamer and MXL timelines
    pub initial_time: Option<InitialTime>,
}

/// State for video rendering.
///
/// Tracks the current position in the MXL video ring buffer and maintains
/// frame rate information for timestamp calculations.
pub(crate) struct VideoState {
    /// MXL grain writer (provides access to video ring buffer)
    pub writer: GrainWriter,

    /// Current grain index in the ring buffer
    pub grain_index: u64,

    /// Frame rate (numerator/denominator, e.g., 30000/1001 for 29.97fps)
    pub grain_rate: Rational,

    /// Number of grains in the ring buffer
    pub grain_count: u32,
}

/// State for audio rendering.
///
/// Manages writing audio samples to the MXL continuous (non-discrete) flow.
pub(crate) struct AudioState {
    /// MXL samples writer (provides access to audio ring buffer)
    pub writer: SamplesWriter,

    /// Bit depth per sample (typically 32 for F32LE)
    pub bit_depth: u8,

    /// Number of samples per write batch (affects latency vs. efficiency)
    pub batch_size: usize,

    /// MXL flow definition (sample rate, channel count, etc.)
    pub flow_def: FlowDefAudio,

    /// Next write position in the ring buffer (None until first write)
    pub next_write_index: Option<u64>,
}

/// Context wrapper for the element's mutable state.
///
/// This allows state to be Option<State> so it can be created in start()
/// and destroyed in stop(), following GStreamer's state transition lifecycle.
#[derive(Default)]
pub(crate) struct Context {
    /// The element's state (None when stopped, Some when started)
    pub state: Option<State>,
}

/// Timeline synchronization information.
///
/// Stores the offset needed to convert between GStreamer's running time
/// (relative to pipeline start) and MXL's absolute timestamp indices.
#[derive(Default, Debug, Clone)]
pub(crate) struct InitialTime {
    /// Offset to add to GStreamer time to get MXL time
    pub mxl_to_gst_offset: ClockTime,
}

/// Initializes audio state from GStreamer audio caps.
///
/// Called when the sink receives audio/x-raw caps during negotiation. Creates
/// an MXL flow definition (NMOS-compatible JSON), initializes the MXL flow writer,
/// and stores audio-specific state for rendering buffers.
///
/// # Arguments
/// * `state` - Mutable reference to the element's runtime state
/// * `info` - GStreamer AudioInfo extracted from negotiated caps
/// * `flow_id` - UUID string for the MXL flow (from element properties)
///
/// # Returns
/// * `Ok(())` if the flow was created/attached successfully
/// * `Err(LoggableError)` if flow creation failed or UUID is invalid
///
/// # MXL Flow Definition
/// The function builds an NMOS-style JSON flow definition with:
/// - Sample rate (e.g., 48000/1)
/// - Channel count
/// - Bit depth
/// - Media type ("audio/float32")
pub(crate) fn init_state_with_audio(
    state: &mut State,
    info: AudioInfo,
    flow_id: &str,
) -> Result<(), gst::LoggableError> {
    // Extract audio parameters from GStreamer AudioInfo
    let channels = info.channels() as i32;
    let rate = info.rate() as i32;
    let bit_depth = info.depth() as u8;
    let format = info.format().to_string();

    // Create NMOS grouphint tag with process ID for flow identification
    let pid = process::id();
    let mut tags = HashMap::new();
    tags.insert(
        "urn:x-nmos:tag:grouphint/v1.0".to_string(),
        vec![format!("Media Function {}:Audio", pid).to_string()],
    );

    // Build MXL flow definition (audio-specific details)
    let flow_def_details = FlowDefAudio {
        sample_rate: Rate {
            numerator: rate,
            denominator: 1,
        },
        channel_count: channels,
        bit_depth,
    };

    // Build complete NMOS-compatible flow definition
    let flow_def = FlowDef {
        id: Uuid::parse_str(flow_id)
            .map_err(|e| gst::loggable_error!(CAT, "Flow ID is invalid: {}", e))?,
        description: "MXL Audio Flow".into(),
        format: "urn:x-nmos:format:audio".into(),
        tags,
        label: "MXL Audio Flow".into(),
        media_type: "audio/float32".to_string(),
        parents: vec![],
        details: FlowDefDetails::Audio(flow_def_details.clone()),
    };

    let instance = &state.instance;

    // Create flow writer (creates new flow or attaches to existing)
    let (flow_writer, flow, is_created) = instance
        .create_flow_writer(
            serde_json::to_string(&flow_def)
                .map_err(|e| gst::loggable_error!(CAT, "Failed to convert: {}", e))?
                .as_str(),
            None,
        )
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create flow writer: {}", e))?;

    // Convert generic flow writer to samples writer (audio-specific API)
    let writer = flow_writer
        .to_samples_writer()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?;

    // Verify we successfully created the writer (not blocked by existing writer)
    if !is_created {
        return Err(gst::loggable_error!(
            CAT,
            "The writer could not be created, the UUID belongs to a flow with another active writer"
        ));
    }

    // Initialize audio state with writer and flow configuration
    state.audio = Some(AudioState {
        writer,
        bit_depth,
        batch_size: flow.common().max_commit_batch_size_hint() as usize,
        flow_def: flow_def_details,
        next_write_index: None,
    });
    state.flow = Some(flow);

    trace!(
        "Made it to the end of set_caps with format {}, channel_count {}, sample_rate {}, bit_depth {}",
        format, channels, rate, bit_depth
    );
    Ok(())
}

/// Initializes video state from GStreamer video caps.
///
/// Called when the sink receives video/x-raw caps during negotiation. Extracts
/// video parameters (resolution, framerate, etc.), creates an MXL flow definition,
/// and initializes the grain writer for rendering video buffers.
///
/// # Arguments
/// * `state` - Mutable reference to the element's runtime state
/// * `structure` - GStreamer caps structure containing video format details
/// * `flow_id` - UUID string for the MXL flow
///
/// # Returns
/// * `Ok(())` if the video flow was initialized successfully
/// * `Err(LoggableError)` if flow creation failed
///
/// # Video Format
/// Currently supports v210 (10-bit 4:2:2 YUV), commonly used in broadcast.
/// The function creates three components (Y, Cb, Cr) with appropriate dimensions.
pub(crate) fn init_state_with_video(
    state: &mut State,
    structure: &StructureRef,
    flow_id: &str,
) -> Result<(), gst::LoggableError> {
    // Extract video parameters from caps structure (with fallback defaults)
    let format = structure
        .get::<String>("format")
        .unwrap_or_else(|_| "v210".to_string());
    let width = structure.get::<i32>("width").unwrap_or(1920);
    let height = structure.get::<i32>("height").unwrap_or(1080);
    let framerate = structure
        .get::<gst::Fraction>("framerate")
        .unwrap_or_else(|_| gst::Fraction::new(30000, 1001));
    let interlace = structure
        .get::<String>("interlace-mode")
        .unwrap_or_else(|_| "progressive".to_string());
    let interlace_mode =
        InterlaceMode::from_str(interlace.as_str()).unwrap_or(InterlaceMode::Progressive);
    let colorimetry = structure
        .get::<String>("colorimetry")
        .unwrap_or_else(|_| "BT709".to_string());

    // Create NMOS grouphint tag for flow identification
    let pid = process::id();
    let mut tags = HashMap::new();
    tags.insert(
        "urn:x-nmos:tag:grouphint/v1.0".to_string(),
        vec![format!("Media Function {}:Video", pid).to_string()],
    );
    // Build video flow definition with YUV 4:2:2 component structure
    // (Y is full resolution, Cb/Cr are half width for 4:2:2 subsampling)
    let flow_def_details = FlowDefVideo {
        grain_rate: Rate {
            numerator: framerate.numer(),
            denominator: framerate.denom(),
        },
        frame_width: width,
        frame_height: height,
        interlace_mode,
        colorspace: colorimetry,
        components: vec![
            Component {
                name: "Y".into(),
                width,
                height,
                bit_depth: 10,
            },
            Component {
                name: "Cb".into(),
                width: width / 2,  // Half horizontal resolution for chroma
                height,
                bit_depth: 10,
            },
            Component {
                name: "Cr".into(),
                width: width / 2,  // Half horizontal resolution for chroma
                height,
                bit_depth: 10,
            },
        ],
    };

    // Build complete NMOS flow definition
    let flow_def = FlowDef {
        id: Uuid::parse_str(flow_id)
            .map_err(|e| gst::loggable_error!(CAT, "Flow ID is invalid: {}", e))?,
        description: format!(
            "MXL Test Flow, {}p{}",
            height,
            framerate.numer() / framerate.denom()
        ),
        tags,
        format: "urn:x-nmos:format:video".into(),
        label: format!(
            "MXL Test Flow, {}p{}",
            height,
            framerate.numer() / framerate.denom()
        ),
        parents: vec![],
        media_type: format!("video/{}", format),
        details: mxl::flowdef::FlowDefDetails::Video(flow_def_details),
    };
    let instance = &state.instance;

    // Create flow writer (establishes exclusive write access to the flow)
    let (flow_writer, flow, is_created) = instance
        .create_flow_writer(
            serde_json::to_string(&flow_def)
                .map_err(|e| gst::loggable_error!(CAT, "Failed to convert: {}", e))?
                .as_str(),
            None,
        )
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create flow writer: {}", e))?;

    // Ensure we successfully created the writer
    if !is_created {
        return Err(gst::loggable_error!(
            CAT,
            "The writer could not be created, the UUID belongs to a flow with another active writer"
        ));
    }

    // Convert generic flow writer to grain writer (video-specific API)
    let writer = flow_writer
        .to_grain_writer()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?;

    // Extract flow parameters needed for rendering
    let grain_rate = flow
        .common()
        .grain_rate()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to get grain rate: {}", e))?;
    let grain_count = flow
        .discrete()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to get grain count: {}", e))?
        .grainCount;
    let rate = flow
        .common()
        .grain_rate()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to get grain rate: {}", e))?;

    // Get current MXL index for initial synchronization
    let index = instance.get_current_index(&rate);

    // Initialize video state
    state.video = Some(VideoState {
        writer,
        grain_index: index,
        grain_rate,
        grain_count,
    });
    state.flow = Some(flow);

    Ok(())
}
