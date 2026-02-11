//! Video Buffer Creation for MXL Source
//!
//! This module handles reading video grains from MXL's shared-memory ring buffer
//! and converting them to GStreamer buffers.
//!
//! ## Video Format
//! - Input: MXL grain (one frame of v210 video)
//! - Output: GStreamer buffer with v210 video frame
//!
//! ## Synchronization Strategy
//! 1. **Initialization**: On first frame, establish offset between MXL timeline and GStreamer running time
//! 2. **Index tracking**: Calculate next frame index based on frame counter
//! 3. **Late detection**: Skip missed frames if reader falls behind
//! 4. **Future prevention**: Wait if reader is ahead of current time
//! 5. **PTS calculation**: Generate timestamps from frame counter and rate
//!
//! ## Invalid Frames
//! MXL grains can be flagged as invalid (e.g., dropped frames). These are rejected.

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::Duration;

use crate::mxlsrc::imp::{CreateState, MxlSrc};
use crate::mxlsrc::state::{InitialTime, State};
use glib::subclass::types::ObjectSubclassExt;
use gst::prelude::*;
use gstreamer as gst;
use tracing::trace;

/// Maximum time to wait for a grain to become available
const GET_GRAIN_TIMEOUT: Duration = Duration::from_secs(5);

/// MXL flag indicating invalid/dropped frame
pub(super) const MXL_GRAIN_FLAG_INVALID: u32 = 0x00000001;

/// Creates a GStreamer video buffer from an MXL grain.
///
/// Reads one frame from the MXL ring buffer and wraps it in a GStreamer buffer
/// with proper timestamp.
///
/// # Arguments
/// * `src` - The source element (provides access to pipeline clock)
/// * `state` - Element state containing video reader and timing info
///
/// # Returns
/// * `Ok(CreateState::DataCreated(buffer))` if frame was read successfully
/// * `Ok(CreateState::NoDataCreated)` if grain is unavailable (stale flow)
/// * `Err(FlowError)` if grain is invalid or reading failed
pub(crate) fn create_video(src: &MxlSrc, state: &mut State) -> Result<CreateState, gst::FlowError> {
    // Get video state and frame rate
    let video_state = state.video.as_mut().ok_or(gst::FlowError::Error)?;
    let rate = video_state.grain_rate;
    let current_index = state.instance.get_current_index(&rate);

    // Get GStreamer running time
    let Some(ts_gst) = src.obj().current_running_time() else {
        return Err(gst::FlowError::Error);
    };

    // Initialize timing on first frame
    if !video_state.is_initialized {
        state.initial_info = InitialTime {
            mxl_index: current_index,
            gst_time: ts_gst,
        };
        video_state.is_initialized = true;
    }

    let initial_info = &state.initial_info;

    // Calculate next frame index based on frame counter
    let mut next_frame_index = initial_info.mxl_index + video_state.frame_counter;

    // Detect missed frames (reader lagging behind)
    if next_frame_index < current_index {
        let missed_frames = current_index - next_frame_index;
        trace!(
            "Skipped frames! next_frame_index={} < head_index={} (lagging {})",
            next_frame_index, current_index, missed_frames
        );
        next_frame_index = current_index;
    } else if next_frame_index > current_index {
        // Reader is ahead (waiting for producer)
        let frames_ahead = next_frame_index - current_index;
        trace!(
            "index={} > head_index={} (ahead {} frames)",
            next_frame_index, current_index, frames_ahead
        );
    }

    // Calculate PTS from frame counter (not from MXL index, to avoid drift)
    let pts = (video_state.frame_counter) as u128 * 1_000_000_000u128;
    let pts = pts * rate.denominator as u128;
    let pts = pts / rate.numerator as u128;

    let pts = gst::ClockTime::from_nseconds(pts as u64);

    // Apply timeline offset
    let mut pts = pts + initial_info.gst_time;
    let initial_info = &mut state.initial_info;

    // Adjust offset if buffer would be late
    if pts < ts_gst {
        let prev_pts = pts;
        pts -= initial_info.gst_time;
        initial_info.gst_time = initial_info.gst_time + ts_gst - prev_pts;
        pts += initial_info.gst_time;
    }

    // Read grain from MXL
    let mut buffer;
    {
        trace!("Getting grain with index: {}", next_frame_index);
        let grain_data = match video_state
            .grain_reader
            .get_complete_grain(next_frame_index, GET_GRAIN_TIMEOUT)
        {
            Ok(r) => r,

            Err(err) => {
                // Timeout or flow stale
                trace!("error: {err}");
                return Ok(CreateState::NoDataCreated);
            }
        };

        // Reject invalid frames
        if grain_data.flags & MXL_GRAIN_FLAG_INVALID != 0 {
            return Err(gst::FlowError::Error);
        }

        // Create GStreamer buffer
        buffer =
            gst::Buffer::with_size(grain_data.payload.len()).map_err(|_| gst::FlowError::Error)?;

        {
            let buffer = buffer.get_mut().ok_or(gst::FlowError::Error)?;
            buffer.set_pts(pts);

            // Copy frame data
            let mut map = buffer.map_writable().map_err(|_| gst::FlowError::Error)?;
            map.as_mut_slice().copy_from_slice(grain_data.payload);
        }
    }

    trace!(pts=?buffer.pts(), ts_gst=?ts_gst, buffer=?buffer, "Produced buffer");

    // Advance frame counter
    video_state.frame_counter += 1;
    Ok(CreateState::DataCreated(buffer))
}
