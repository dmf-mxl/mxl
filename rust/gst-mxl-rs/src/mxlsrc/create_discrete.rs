// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::Duration;

use crate::format;
use crate::mxlsrc::imp::{CreateState, MxlSrc};
use crate::mxlsrc::mxl_helper::pts_subtrahend;
use crate::mxlsrc::state::{DiscreteFormat, FlowState, State};
use crate::mxlsrc::timing::{
    ReadStep, discrete_grain_count, flow_head_index, index_period, pts_for_index, resolve_read_step,
};
use gstreamer as gst;
use tracing::trace;

const GET_GRAIN_TIMEOUT: Duration = Duration::from_secs(5);
pub(super) const MXL_GRAIN_FLAG_INVALID: u32 = 0x00000001;

pub(crate) fn create_discrete(
    src: &MxlSrc,
    state: &mut State,
    offset: u64,
) -> Result<CreateState, gst::FlowError> {
    let subtrahend = pts_subtrahend(src, offset)?;
    let instance = &state.instance;
    let discrete_state = match state.flow_state.as_mut() {
        Some(FlowState::Discrete(discrete)) => discrete,
        _ => return Err(gst::FlowError::Error),
    };
    let rate = discrete_state.grain_rate;
    let head = flow_head_index(&discrete_state.grain_reader)?;
    let grain_count = discrete_grain_count(&discrete_state.grain_reader)?;
    let media = match discrete_state.format {
        DiscreteFormat::Video => "video",
        DiscreteFormat::Data => "data",
    };

    if !discrete_state.is_initialized {
        if head == 0 {
            // Flow exists but the producer has not committed a grain yet.
            return Ok(CreateState::NoDataCreated);
        }
        // Attach live at the newest committed grain.
        discrete_state.index = head;
        discrete_state.is_initialized = true;
    }

    let (read_index, jumped) = match resolve_read_step(discrete_state.index, head, grain_count) {
        ReadStep::WaitForProducer => return Ok(CreateState::NoDataCreated),
        ReadStep::Read { index, discont } => (index, discont),
    };
    if jumped {
        trace!("Fell behind ring: jumped to oldest retained grain {read_index} (head={head})");
    }

    trace!("Getting {media} grain with index: {read_index}");
    let grain_data = match discrete_state
        .grain_reader
        .get_complete_grain(read_index, GET_GRAIN_TIMEOUT)
    {
        Ok(grain) => grain,
        Err(err) => {
            trace!("error: {err}");
            return Ok(CreateState::NoDataCreated);
        }
    };
    if grain_data.flags & MXL_GRAIN_FLAG_INVALID != 0 {
        return Err(gst::FlowError::Error);
    }

    // The ring slot may not hold the requested absolute grain: an older grain
    // means it has not been produced yet (wait rather than emit stale data), a
    // newer one means the writer lapped us mid-read (catch up with DISCONT).
    let (read_index, slot_discont) = match grain_data.index {
        actual if actual < read_index => {
            trace!("Slot for index {read_index} still holds {actual}; waiting for producer");
            return Ok(CreateState::NoDataCreated);
        }
        actual if actual > read_index => {
            trace!("Fell behind ring: requested {read_index}, slot holds {actual}");
            (actual, true)
        }
        actual => (actual, false),
    };

    let Some(pts) = pts_for_index(instance, read_index, &rate, subtrahend)? else {
        // Grain committed before the pipeline base time maps to a negative
        // running time (before the consumer joined); a live source must not emit
        // it. Skip forward rather than clamp its PTS to 0 (which would corrupt
        // the first inter-frame interval). Both readers share `subtrahend`, so
        // they skip the same grains and stay index-aligned.
        trace!("Skipping pre-start grain {read_index} (running time would be negative)");
        discrete_state.next_discont |= jumped || slot_discont;
        discrete_state.index = read_index + 1;
        return Ok(CreateState::NoDataCreated);
    };
    let deferred_discont = std::mem::take(&mut discrete_state.next_discont);
    let is_discont = jumped || slot_discont || deferred_discont;

    let mut buffer = match discrete_state.format {
        DiscreteFormat::Video => gst::Buffer::from_slice(grain_data.payload.to_vec()),
        DiscreteFormat::Data => {
            let st2038 = format::data::gst_st2038_from_mxl_smpte291_grain(grain_data.payload)
                .map_err(|_| gst::FlowError::Error)?;
            gst::Buffer::from_slice(st2038)
        }
    };
    {
        let buffer = buffer.get_mut().ok_or(gst::FlowError::Error)?;
        buffer.set_pts(pts);
        buffer.set_duration(index_period(&rate));
        if is_discont {
            buffer.set_flags(gst::BufferFlags::DISCONT);
        }
    }

    trace!(pts = ?buffer.pts(), index = read_index, "Produced {media} buffer");
    discrete_state.index = read_index + 1;
    Ok(CreateState::DataCreated(buffer))
}
