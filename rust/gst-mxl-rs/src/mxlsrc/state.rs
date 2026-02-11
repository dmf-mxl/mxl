//! State Management for MXL Source
//!
//! This module defines the runtime state structures used by mxlsrc, including:
//! - User settings (flow IDs, domain)
//! - Runtime state (MXL readers, timing information)
//! - Video/audio-specific state (frame/sample counters, synchronization)
//!
//! ## Key Differences from Sink
//! - Uses **readers** (not writers) to access MXL flows
//! - Tracks **read position** (not write position)
//! - Handles **producer-consumer synchronization** (waits for data from writer)

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use gstreamer as gst;

use mxl::{FlowReader, GrainReader, MxlInstance, Rational, SamplesReader};

/// Default value for flow-id properties (empty = must be set by user)
pub(crate) const DEFAULT_FLOW_ID: &str = "";

/// Default value for domain property (empty = must be set by user)
pub(crate) const DEFAULT_DOMAIN: &str = "";

/// Timeline synchronization information.
///
/// Stores the initial MXL index and GStreamer running time to establish
/// a fixed offset for timestamp conversion.
#[derive(Debug, Default, Clone)]
pub struct InitialTime {
    /// MXL index when first buffer was produced
    pub mxl_index: u64,

    /// GStreamer running time when first buffer was produced
    pub gst_time: gst::ClockTime,
}

/// User-configurable settings for the mxlsrc element.
///
/// Set via GObject properties before entering PLAYING state.
#[derive(Debug, Clone)]
pub struct Settings {
    /// UUID of video flow to read from (mutually exclusive with audio_flow)
    pub video_flow: Option<String>,

    /// UUID of audio flow to read from (mutually exclusive with video_flow)
    pub audio_flow: Option<String>,

    /// Shared memory domain path (e.g., "/dev/shm")
    pub domain: String,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            video_flow: None,
            audio_flow: None,
            domain: DEFAULT_DOMAIN.to_owned(),
        }
    }
}

/// Runtime state for the mxlsrc element.
///
/// Holds the MXL instance, flow reader, and format-specific state.
/// Only one of `video` or `audio` will be Some(_) depending on properties.
pub struct State {
    /// MXL SDK instance (handles shared memory communication)
    pub instance: MxlInstance,

    /// Timestamp synchronization info
    pub initial_info: InitialTime,

    /// Video-specific state (present if video-flow-id is set)
    pub video: Option<VideoState>,

    /// Audio-specific state (present if audio-flow-id is set)
    pub audio: Option<AudioState>,
}

/// State for video reading.
///
/// Tracks the current frame position in the MXL video ring buffer.
pub struct VideoState {
    /// Frame rate (numerator/denominator)
    pub grain_rate: Rational,

    /// Number of frames produced so far
    pub frame_counter: u64,

    /// True after first frame (timing is initialized)
    pub is_initialized: bool,

    /// MXL grain reader (provides access to video ring buffer)
    pub grain_reader: GrainReader,
}

/// State for audio reading.
///
/// Manages reading audio samples from the MXL continuous flow.
pub struct AudioState {
    /// Generic flow reader (for metadata queries)
    pub reader: FlowReader,

    /// MXL samples reader (provides access to audio ring buffer)
    pub samples_reader: SamplesReader,

    /// Number of batches read so far
    pub batch_counter: u64,

    /// True after first batch (timing is initialized)
    pub is_initialized: bool,

    /// Current read index in the ring buffer
    pub index: u64,

    /// True if next buffer should have DISCONT flag (discontinuity detected)
    pub next_discont: bool,
}

/// Context wrapper for the element's mutable state.
///
/// Allows state to be None when stopped and Some when started.
#[derive(Default)]
pub struct Context {
    /// The element's state (None when stopped, Some when started)
    pub state: Option<State>,
}
