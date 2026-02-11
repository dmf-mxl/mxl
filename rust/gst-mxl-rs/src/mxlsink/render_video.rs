//! Video Buffer Rendering for MXL Sink
//!
//! This module handles the conversion of GStreamer video buffers to MXL grains
//! and writes them to the MXL discrete (frame-based) ring buffer.
//!
//! ## Video Format
//! - Input: GStreamer buffer with v210 format (10-bit 4:2:2 YUV)
//! - Output: MXL grain (one frame per grain)
//!
//! ## Synchronization Strategy
//! The renderer synchronizes video frames with the pipeline clock:
//! 1. On first frame: establishes offset between GStreamer running time and MXL timeline
//! 2. For each frame: maps buffer PTS (presentation timestamp) to MXL grain index
//! 3. Prevents future writes (caps writes ahead of current time)
//! 4. Updates timing offset if buffer is late (drift correction)
//!
//! ## Ring Buffer Management
//! - MXL discrete flows use indexed grains (one per frame)
//! - The grain_index determines which slot in the ring buffer to write
//! - Out-of-range indices are clamped to prevent buffer overflow

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::mxlsink::{self, state::InitialTime};

use glib::subclass::types::ObjectSubclassExt;
use gst::{ClockTime, prelude::*};
use gstreamer as gst;
use tracing::trace;

/// Renders a GStreamer video buffer to the MXL grain writer.
///
/// Synchronizes the buffer's PTS with the MXL timeline, writes the frame data
/// to the appropriate grain in the ring buffer, and advances the grain index.
///
/// # Arguments
/// * `mxlsink` - The sink element (provides access to pipeline clock)
/// * `state` - Element state containing video writer and timing info
/// * `buffer` - GStreamer buffer with v210 video frame
///
/// # Returns
/// * `Ok(FlowSuccess::Ok)` if the frame was written successfully
/// * `Err(FlowError)` if writing failed or state is invalid
///
/// # Timing
/// Uses buffer PTS (if present) to determine the target grain index.
/// Falls back to current MXL index if PTS is unavailable.
pub(crate) fn video(
    mxlsink: &mxlsink::imp::MxlSink,
    state: &mut mxlsink::state::State,
    buffer: &gst::Buffer,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    // Get current MXL index based on grain rate
    let current_index = state.instance.get_current_index(
        &state
            .flow
            .as_ref()
            .ok_or(gst::FlowError::Error)?
            .common()
            .grain_rate()
            .map_err(|_| gst::FlowError::Error)?,
    );
    let video_state = state.video.as_mut().ok_or(gst::FlowError::Error)?;

    // Get GStreamer running time (time since pipeline started)
    let gst_time = mxlsink
        .obj()
        .current_running_time()
        .ok_or(gst::FlowError::Error)?;

    // Initialize timing offset on first frame
    let initial_info = state.initial_time.get_or_insert(InitialTime {
        mxl_to_gst_offset: ClockTime::from_nseconds(state.instance.get_time()) - gst_time,
    });

    // Determine target grain index from buffer PTS
    let mut index = current_index;
    match buffer.pts() {
        Some(pts) => {
            // Convert GStreamer PTS to MXL timeline
            let mxl_pts = pts + initial_info.mxl_to_gst_offset;

            // Map timestamp to grain index
            index = state
                .instance
                .timestamp_to_index(mxl_pts.nseconds(), &video_state.grain_rate)
                .map_err(|_| gst::FlowError::Error)?;

            trace!(
                "PTS {:?} mapped to grain index {}, current index is {} and running time is {} delta= {}",
                mxl_pts,
                index,
                current_index,
                gst_time,
                mxl_pts.saturating_sub(gst_time)
            );

            // Clamp index if too far in the future (prevent write beyond ring buffer)
            if index > current_index && index - current_index > video_state.grain_count as u64 {
                index = current_index + video_state.grain_count as u64 - 1;
            }
            video_state.grain_index = index;
        }
        None => {
            // No PTS: use current index (live mode)
            video_state.grain_index = current_index;
        }
    }

    // Write frame data to the ring buffer
    commit_buffer(buffer, video_state, index)?;

    // Advance to next grain
    video_state.grain_index += 1;
    trace!("END RENDER");
    Ok(gst::FlowSuccess::Ok)
}

/// Commits a video buffer to the MXL ring buffer.
///
/// Opens a grain for writing, copies frame data, and commits the grain
/// (making it visible to readers).
///
/// # Arguments
/// * `buffer` - GStreamer buffer containing v210 frame data
/// * `video_state` - Video state with grain writer
/// * `index` - Target grain index in the ring buffer
///
/// # Returns
/// * `Ok(())` if the frame was written successfully
/// * `Err(FlowError)` if write access failed or commit failed
fn commit_buffer(
    buffer: &gst::Buffer,
    video_state: &mut mxlsink::state::VideoState,
    index: u64,
) -> Result<(), gst::FlowError> {
    // Map buffer for read-only access to frame data
    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let data = map.as_slice();

    // Open write access to the specified grain
    let mut access = video_state
        .writer
        .open_grain(index)
        .map_err(|_| gst::FlowError::Error)?;

    // Get mutable payload (where frame data is stored)
    let payload = access.payload_mut();

    // Copy frame data (handles size mismatch gracefully)
    let copy_len = std::cmp::min(payload.len(), data.len());
    payload[..copy_len].copy_from_slice(&data[..copy_len]);

    // Commit the grain with all slices (makes frame visible to readers)
    let total_slices = access.total_slices();
    access
        .commit(total_slices)
        .map_err(|_| gst::FlowError::Error)?;

    Ok(())
}
