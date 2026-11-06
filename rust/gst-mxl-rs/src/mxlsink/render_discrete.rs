// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::borrow::Cow;

use crate::format;
use crate::format::video;
use crate::mxlsink::{
    self,
    state::{DiscreteFormat, DiscreteState, FlowState},
};

use gstreamer::{self as gst, prelude::ElementExt};
use gstreamer_video::{self as gst_video, VideoFrameExt};
use tracing::trace;

pub(crate) fn discrete(
    state: &mut mxlsink::state::State,
    element: &gst::Element,
    buffer: &gst::Buffer,
    offset: u64,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let base_time = element.base_time().ok_or(gst::FlowError::Error)?;
    let gst_pts = buffer.pts().ok_or(gst::FlowError::Error)?;

    let flow_config = state.flow_config.as_ref().ok_or(gst::FlowError::Error)?;
    let grain_rate = flow_config
        .common()
        .grain_rate()
        .map_err(|_| gst::FlowError::Error)?;

    let mxl_ts = gst_pts
        .nseconds()
        .checked_add(base_time.nseconds())
        .and_then(|timestamp| timestamp.checked_add(offset))
        .ok_or(gst::FlowError::Error)?;
    let discrete_state = match state.flow_state.as_ref() {
        Some(FlowState::Discrete(discrete)) => discrete,
        _ => return Err(gst::FlowError::Error),
    };
    let media = match discrete_state.format {
        DiscreteFormat::Video | DiscreteFormat::VideoAlpha(_) => "VIDEO",
        DiscreteFormat::Data => "DATA",
    };
    trace!("{media} gst PTS: {:#?}", gst_pts);
    trace!("{media} mapped mxl timestamp: {:#?}", mxl_ts);
    let mxl_index = state
        .instance
        .timestamp_to_index(mxl_ts, &grain_rate)
        .map_err(|_| gst::FlowError::Error)?;
    trace!("{media} mapped mxl_index from pts: {:#?}", mxl_index);

    // GstBaseSink (sync=true) has already waited for this buffer's running time,
    // so commit straight to the ring here: no separate pacing.
    if let DiscreteFormat::VideoAlpha(info) = &discrete_state.format {
        let frame = gst_video::VideoFrameRef::from_buffer_ref_readable(buffer.as_ref(), info)
            .map_err(|_| gst::FlowError::Error)?;
        commit_v210a_grain(&frame, discrete_state, mxl_index)?;
        return Ok(gst::FlowSuccess::Ok);
    }

    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let payload = match discrete_state.format {
        DiscreteFormat::Video | DiscreteFormat::VideoAlpha(_) => Cow::Borrowed(map.as_slice()),
        DiscreteFormat::Data => Cow::Owned(
            format::data::mxl_smpte291_grain_from_gst_st2038(map.as_slice())
                .map_err(|_| gst::FlowError::Error)?,
        ),
    };
    commit_grain(payload.as_ref(), discrete_state, mxl_index)?;

    Ok(gst::FlowSuccess::Ok)
}

fn commit_grain(
    payload: &[u8],
    discrete_state: &DiscreteState,
    index: u64,
) -> Result<(), gst::FlowError> {
    let mut access = discrete_state
        .writer
        .open_grain(index)
        .map_err(|_| gst::FlowError::Error)?;
    let destination = access.payload_mut();
    let copy_len = std::cmp::min(destination.len(), payload.len());
    destination[..copy_len].copy_from_slice(&payload[..copy_len]);
    let total_slices = access.total_slices();
    access
        .commit(total_slices)
        .map_err(|_| gst::FlowError::Error)?;
    Ok(())
}

/// Writes an `A422_10LE` frame straight into the grain as `video/v210a`.
fn commit_v210a_grain(
    frame: &gst_video::VideoFrameRef<&gst::BufferRef>,
    discrete_state: &DiscreteState,
    index: u64,
) -> Result<(), gst::FlowError> {
    let plane = |i: u32| -> Result<video::Plane<'_>, gst::FlowError> {
        let stride = frame
            .plane_stride()
            .get(i as usize)
            .and_then(|&s| usize::try_from(s).ok())
            .ok_or(gst::FlowError::Error)?;
        let data = frame.plane_data(i).map_err(|_| gst::FlowError::Error)?;
        Ok(video::Plane { data, stride })
    };
    let planes = [plane(0)?, plane(1)?, plane(2)?, plane(3)?];

    let mut access = discrete_state
        .writer
        .open_grain(index)
        .map_err(|_| gst::FlowError::Error)?;
    video::mxl_v210a_grain_from_gst_a422_10le(
        &planes,
        frame.width() as usize,
        frame.height() as usize,
        access.payload_mut(),
    )
    .map_err(|err| {
        trace!("A422_10LE frame does not fit the v210a grain: {err}");
        gst::FlowError::Error
    })?;
    let total_slices = access.total_slices();
    access
        .commit(total_slices)
        .map_err(|_| gst::FlowError::Error)?;
    Ok(())
}
