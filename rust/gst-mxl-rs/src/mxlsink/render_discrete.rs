// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::borrow::Cow;

use crate::format;
use crate::mxlsink::{
    self,
    state::{DiscreteFormat, DiscreteState, FlowState},
};

use gstreamer::{self as gst, prelude::ElementExt};
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
        DiscreteFormat::Video => "VIDEO",
        DiscreteFormat::Data => "DATA",
    };
    trace!("{media} gst PTS: {:#?}", gst_pts);
    trace!("{media} mapped mxl timestamp: {:#?}", mxl_ts);
    let mxl_index = state
        .instance
        .timestamp_to_index(mxl_ts, &grain_rate)
        .map_err(|_| gst::FlowError::Error)?;
    trace!("{media} mapped mxl_index from pts: {:#?}", mxl_index);

    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let payload = match discrete_state.format {
        DiscreteFormat::Video => Cow::Borrowed(map.as_slice()),
        DiscreteFormat::Data => Cow::Owned(
            format::data::mxl_smpte291_grain_from_gst_st2038(map.as_slice())
                .map_err(|_| gst::FlowError::Error)?,
        ),
    };
    // GstBaseSink (sync=true) has already waited for this buffer's running time,
    // so commit straight to the ring here: no separate pacing.
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
