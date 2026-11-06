// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::borrow::Cow;

use crate::format;
use crate::mxlsink::{
    self,
    imp::CAT,
    state::{DiscreteFormat, DiscreteState, FlowState},
};

use gstreamer as gst;
use tracing::trace;

/// Decide which flow index a grain is written to, given the index its timestamp
/// maps to. Returns `None` when that index was already written, because the
/// writer only accepts indices after the last committed one.
pub(crate) fn place_grain(next_index: Option<u64>, ts_index: u64) -> Option<u64> {
    match next_index {
        Some(next) if ts_index < next => None,
        _ => Some(ts_index),
    }
}

pub(crate) fn discrete(
    state: &mut mxlsink::state::State,
    element: &gst::Element,
    buffer: &gst::Buffer,
    mxl_ts: u64,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let flow_config = state.flow_config.as_ref().ok_or(gst::FlowError::Error)?;
    let grain_rate = flow_config
        .common()
        .grain_rate()
        .map_err(|_| gst::FlowError::Error)?;

    let discrete_state = match state.flow_state.as_mut() {
        Some(FlowState::Discrete(discrete)) => discrete,
        _ => return Err(gst::FlowError::Error),
    };
    let media = match discrete_state.format {
        DiscreteFormat::Video => "VIDEO",
        DiscreteFormat::Data => "DATA",
    };
    trace!("{media} mapped mxl timestamp: {:#?}", mxl_ts);
    let phase = match discrete_state.phase {
        Some(phase) => phase,
        None => {
            let index = state
                .instance
                .timestamp_to_index(mxl_ts, &grain_rate)
                .map_err(|_| gst::FlowError::Error)?;
            let grain_ts = state
                .instance
                .index_to_timestamp(index, &grain_rate)
                .map_err(|_| gst::FlowError::Error)?;
            let phase = mxl_ts as i64 - grain_ts as i64;
            discrete_state.phase = Some(phase);
            phase
        }
    };
    let mxl_index = state
        .instance
        .timestamp_to_index((mxl_ts as i64 - phase) as u64, &grain_rate)
        .map_err(|_| gst::FlowError::Error)?;
    trace!(
        "{media} mapped mxl_index from running time: {:#?}",
        mxl_index
    );
    let Some(mxl_index) = place_grain(discrete_state.next_index, mxl_index) else {
        gst::warning!(
            CAT,
            obj = element,
            "Dropping {} grain at index {}, which was already written (next index {:?})",
            media,
            mxl_index,
            discrete_state.next_index,
        );
        return Ok(gst::FlowSuccess::Ok);
    };

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
    commit_grain(element, payload.as_ref(), discrete_state, mxl_index)?;

    Ok(gst::FlowSuccess::Ok)
}

fn commit_grain(
    element: &gst::Element,
    payload: &[u8],
    discrete_state: &mut DiscreteState,
    index: u64,
) -> Result<(), gst::FlowError> {
    let mut access = discrete_state.writer.open_grain(index).map_err(|e| {
        gst::error!(
            CAT,
            obj = element,
            "Failed to open grain at index {} (next index {:?}): {}",
            index,
            discrete_state.next_index,
            e
        );
        gst::FlowError::Error
    })?;
    let destination = access.payload_mut();
    let copy_len = std::cmp::min(destination.len(), payload.len());
    destination[..copy_len].copy_from_slice(&payload[..copy_len]);
    let total_slices = access.total_slices();
    access.commit(total_slices).map_err(|e| {
        gst::error!(
            CAT,
            obj = element,
            "Failed to commit grain at index {}: {}",
            index,
            e
        );
        gst::FlowError::Error
    })?;
    discrete_state.next_index = Some(index + 1);
    Ok(())
}
