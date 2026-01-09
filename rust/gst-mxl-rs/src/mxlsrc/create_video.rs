// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::Duration;

use crate::mxlsrc::imp::{CreateState, MxlSrc};
use crate::mxlsrc::state::{InitialTime, State};
use glib::subclass::types::ObjectSubclassExt;
use gst::prelude::*;
use gstreamer as gst;
use tracing::trace;

const GET_GRAIN_TIMEOUT: Duration = Duration::from_secs(5);
pub(super) const MXL_GRAIN_FLAG_INVALID: u32 = 0x00000001;

pub(crate) fn create_video(src: &MxlSrc, state: &mut State) -> Result<CreateState, gst::FlowError> {
    let video_state = state.video.as_mut().ok_or(gst::FlowError::Error)?;
    let rate = video_state.grain_rate;
    let current_index = state.instance.get_current_index(&rate);

    let Some(ts_gst) = src.obj().current_running_time() else {
        return Err(gst::FlowError::Error);
    };
    if !video_state.is_initialized {
        state.initial_info = InitialTime {
            mxl_index: current_index,
            gst_time: ts_gst,
        };
        video_state.is_initialized = true;
    }

    let initial_info = &state.initial_info;

    let mut next_frame_index = initial_info.mxl_index + video_state.frame_counter;
    if next_frame_index < current_index {
        let missed_frames = current_index - next_frame_index;
        trace!(
            "Skipped frames! next_frame_index={} < head_index={} (lagging {})",
            next_frame_index, current_index, missed_frames
        );
        next_frame_index = current_index;
    } else if next_frame_index > current_index {
        let frames_ahead = next_frame_index - current_index;
        trace!(
            "index={} > head_index={} (ahead {} frames)",
            next_frame_index, current_index, frames_ahead
        );
    }

    let pts = (video_state.frame_counter) as u128 * 1_000_000_000u128;
    let pts = pts * rate.denominator as u128;
    let pts = pts / rate.numerator as u128;

    let pts = gst::ClockTime::from_nseconds(pts as u64);

    let mut pts = pts + initial_info.gst_time;
    let initial_info = &mut state.initial_info;
    if pts < ts_gst {
        let prev_pts = pts;
        pts -= initial_info.gst_time;
        initial_info.gst_time = initial_info.gst_time + ts_gst - prev_pts;
        pts += initial_info.gst_time;
    }

    let mut buffer;
    {
        trace!("Getting grain with index: {}", next_frame_index);
        let grain_data = match video_state
            .grain_reader
            .get_complete_grain(next_frame_index, GET_GRAIN_TIMEOUT)
        {
            Ok(r) => r,

            Err(err) => {
                trace!("error: {err}");
                return Ok(CreateState::NoDataCreated);
            }
        };
        if grain_data.flags & MXL_GRAIN_FLAG_INVALID != 0 {
            return Err(gst::FlowError::Error);
        }
        buffer =
            gst::Buffer::with_size(grain_data.payload.len()).map_err(|_| gst::FlowError::Error)?;

        {
            let buffer = buffer.get_mut().ok_or(gst::FlowError::Error)?;
            buffer.set_pts(pts);
            let mut map = buffer.map_writable().map_err(|_| gst::FlowError::Error)?;
            map.as_mut_slice().copy_from_slice(grain_data.payload);
        }
    }

    trace!(pts=?buffer.pts(), ts_gst=?ts_gst, buffer=?buffer, "Produced buffer");

    video_state.frame_counter += 1;
    Ok(CreateState::DataCreated(buffer))
}
