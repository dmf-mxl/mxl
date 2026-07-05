// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::Duration;

use crate::mxlsrc::imp::{CreateState, MxlSrc};
use crate::mxlsrc::mxl_helper::pts_subtrahend;
use crate::mxlsrc::state::State;
use crate::mxlsrc::timing::{
    ReadStep, discrete_grain_count, flow_head_index, index_period, pts_for_index, resolve_read_step,
};
use gstreamer as gst;
use tracing::trace;

const GET_GRAIN_TIMEOUT: Duration = Duration::from_secs(5);
pub(super) const MXL_GRAIN_FLAG_INVALID: u32 = 0x00000001;

pub(crate) fn create_video(
    src: &MxlSrc,
    state: &mut State,
    offset: u64,
) -> Result<CreateState, gst::FlowError> {
    let subtrahend = pts_subtrahend(src, offset)?;
    let instance = &state.instance;
    let video_state = state.video.as_mut().ok_or(gst::FlowError::Error)?;
    let rate = video_state.grain_rate;
    let head = flow_head_index(&video_state.grain_reader)?;
    let grain_count = discrete_grain_count(&video_state.grain_reader)?;

    if !video_state.is_initialized {
        if head == 0 {
            // Flow exists but the producer has not committed a grain yet.
            return Ok(CreateState::NoDataCreated);
        }
        // Attach live at the newest committed grain.
        video_state.index = head;
        video_state.is_initialized = true;
    }

    let (read_index, jumped) = match resolve_read_step(video_state.index, head, grain_count) {
        ReadStep::WaitForProducer => return Ok(CreateState::NoDataCreated),
        ReadStep::Read { index, discont } => (index, discont),
    };
    if jumped {
        trace!("Fell behind ring: jumped to oldest retained grain {read_index} (head={head})");
    }

    trace!("Getting video grain with index: {read_index}");
    let grain_data = match video_state
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
        video_state.next_discont |= jumped || slot_discont;
        video_state.index = read_index + 1;
        return Ok(CreateState::NoDataCreated);
    };
    let is_discont = jumped || slot_discont || std::mem::take(&mut video_state.next_discont);

    let mut buffer =
        gst::Buffer::with_size(grain_data.payload.len()).map_err(|_| gst::FlowError::Error)?;
    {
        let buffer = buffer.get_mut().ok_or(gst::FlowError::Error)?;
        buffer.set_pts(pts);
        buffer.set_duration(index_period(&rate));
        if is_discont {
            buffer.set_flags(gst::BufferFlags::DISCONT);
        }
        let mut map = buffer.map_writable().map_err(|_| gst::FlowError::Error)?;
        map.as_mut_slice().copy_from_slice(grain_data.payload);
    }

    trace!(pts = ?buffer.pts(), index = read_index, "Produced video buffer");
    video_state.index = read_index + 1;
    Ok(CreateState::DataCreated(buffer))
}
