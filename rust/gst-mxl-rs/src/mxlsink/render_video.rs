// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::mxlsink;

use gstreamer as gst;
use tracing::trace;

pub(crate) fn video(
    state: &mut mxlsink::state::State,
    buffer: &gst::Buffer,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let flow = state.flow.as_ref().ok_or(gst::FlowError::Error)?;
    let grain_rate = flow
        .common()
        .grain_rate()
        .map_err(|_| gst::FlowError::Error)?;

    let current_index = state.instance.get_current_index(&grain_rate);

    let video_state = state.video.as_mut().ok_or(gst::FlowError::Error)?;

    let buffer_size = video_state.grain_count as u64;
    let safe_window = buffer_size / 2;

    let next_index = video_state.grain_index;

    let diff = next_index as i128 - current_index as i128;

    if diff >= safe_window as i128 {
        return Ok(gst::FlowSuccess::Ok);
    }

    commit_buffer(buffer, video_state, next_index)?;
    video_state.grain_index = video_state.grain_index.wrapping_add(1);

    trace!("Committed grain {}", next_index);

    Ok(gst::FlowSuccess::Ok)
}

fn commit_buffer(
    buffer: &gst::Buffer,
    video_state: &mut mxlsink::state::VideoState,
    index: u64,
) -> Result<(), gst::FlowError> {
    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let data = map.as_slice();
    let mut access = video_state
        .writer
        .open_grain(index)
        .map_err(|_| gst::FlowError::Error)?;
    let payload = access.payload_mut();
    let copy_len = std::cmp::min(payload.len(), data.len());
    payload[..copy_len].copy_from_slice(&data[..copy_len]);
    let total_slices = access.total_slices();
    access
        .commit(total_slices)
        .map_err(|_| gst::FlowError::Error)?;
    Ok(())
}
