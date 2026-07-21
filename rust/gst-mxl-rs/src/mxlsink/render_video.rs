// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::mxlsink::{self, state::VideoState};

use gstreamer::{self as gst, prelude::ElementExt};
use tracing::trace;

pub(crate) fn video(
    state: &mut mxlsink::state::State,
    element: &gst::Element,
    buffer: &gst::Buffer,
    offset: u64,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let base_time = element.base_time().ok_or(gst::FlowError::Error)?;
    let gst_pts = buffer.pts().ok_or(gst::FlowError::Error)?;

    let flow = state.flow.as_ref().ok_or(gst::FlowError::Error)?;
    let grain_rate = flow
        .common()
        .grain_rate()
        .map_err(|_| gst::FlowError::Error)?;

    let mxl_ts = gst_pts
        .nseconds()
        .checked_add(base_time.nseconds())
        .and_then(|timestamp| timestamp.checked_add(offset))
        .ok_or(gst::FlowError::Error)?;
    trace!("VIDEO gst PTS: {:#?}", gst_pts);
    trace!("VIDEO mapped mxl timestamp: {:#?}", mxl_ts);
    let mxl_index = state
        .instance
        .timestamp_to_index(mxl_ts, &grain_rate)
        .map_err(|_| gst::FlowError::Error)?;
    trace!("VIDEO mapped mxl_index from pts: {:#?}", mxl_index);

    let video_state = state.video.as_ref().ok_or(gst::FlowError::Error)?;
    // GstBaseSink (sync=true) has already waited for this buffer's running time,
    // so commit straight to the ring here: no separate pacing.
    commit_buffer(buffer, video_state, mxl_index)?;

    Ok(gst::FlowSuccess::Ok)
}

fn commit_buffer(
    buffer: &gst::Buffer,
    video_state: &VideoState,
    index: u64,
) -> Result<(), gst::FlowError> {
    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let src = map.as_slice();
    let mut access = video_state
        .writer
        .open_grain(index)
        .map_err(|_| gst::FlowError::Error)?;
    let payload = access.payload_mut();
    let copy_len = std::cmp::min(payload.len(), src.len());
    payload[..copy_len].copy_from_slice(&src[..copy_len]);
    let total_slices = access.total_slices();
    access
        .commit(total_slices)
        .map_err(|_| gst::FlowError::Error)?;
    Ok(())
}
