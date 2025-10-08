// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::{Duration, Instant, SystemTime};

use crate::mxlsrc::imp::MxlSrc;
use crate::mxlsrc::state::{InitialTime, State};
use glib::subclass::types::ObjectSubclassExt;
use gst::prelude::*;
use gst_base::subclass::base_src::CreateSuccess;
use gstreamer as gst;
use gstreamer_base as gst_base;
use tracing::trace;

const GET_GRAIN_TIMEOUT: Duration = Duration::from_secs(5);
pub(super) const MXL_GRAIN_FLAG_INVALID: u32 = 0x00000001;

pub(crate) fn create_video(
    src: &MxlSrc,
    state: &mut State,
) -> Result<CreateSuccess, gst::FlowError> {
    let video_state = state.video.as_mut().ok_or(gst::FlowError::Error)?;
    let current_index;
    let rate = video_state.grain_rate;
    {
        current_index = state.instance.get_current_index(&rate);
    }
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
    let _ = initial_info;
    let initial_info = &state.initial_info;
    let grain_request_time = Instant::now();
    let real_time_start = SystemTime::now();
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
    let real_time_end = SystemTime::now();
    let elapsed_real = real_time_end
        .duration_since(real_time_start)
        .unwrap_or_default();

    let start = real_time_start
        .duration_since(SystemTime::UNIX_EPOCH)
        .unwrap_or_default();
    let end = real_time_end
        .duration_since(SystemTime::UNIX_EPOCH)
        .unwrap_or_default();
    let start_hms = {
        let total_secs = start.as_secs();
        let hours = total_secs / 3600 % 24;
        let minutes = total_secs / 60 % 60;
        let seconds = total_secs % 60;
        let millis = start.subsec_millis();
        format!("{:02}:{:02}:{:02}.{:03}", hours, minutes, seconds, millis)
    };

    let end_hms = {
        let total_secs = end.as_secs();
        let hours = total_secs / 3600 % 24;
        let minutes = total_secs / 60 % 60;
        let seconds = total_secs % 60;
        let millis = end.subsec_millis();
        format!("{:02}:{:02}:{:02}.{:03}", hours, minutes, seconds, millis)
    };

    trace!(
        "Grain number: {} | Grain request time: {} µs | Real time start: {} | Real time end: {} | Elapsed wall time: {} ms",
        next_frame_index,
        grain_request_time.elapsed().as_micros(),
        start_hms,
        end_hms,
        elapsed_real.as_millis()
    );
    let _ = initial_info;
    let initial_info = &state.initial_info;
    let pts = (video_state.frame_counter) as u128 * 1_000_000_000u128;
    let pts = pts * rate.denominator as u128;
    let pts = pts / rate.numerator as u128;

    let pts = gst::ClockTime::from_nseconds(pts as u64);

    let mut pts = pts + initial_info.gst_time;
    let _ = initial_info;
    let initial_info = &mut state.initial_info;
    if pts < ts_gst {
        let prev_pts = pts;
        pts -= initial_info.gst_time;
        initial_info.gst_time = initial_info.gst_time + ts_gst - prev_pts;
        pts += initial_info.gst_time;
    }

    let mut buffer;
    {
        let binding = &video_state.grain_reader;
        trace!("Getting grain with index: {}", next_frame_index);
        let grain_data = match binding.get_complete_grain(next_frame_index, GET_GRAIN_TIMEOUT) {
            Ok(r) => r,

            Err(err) => {
                trace!("error: {err}");
                return Err(gst::FlowError::Error);
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

    trace!("PTS: {:?} GST-CURRENT: {:?}", buffer.pts(), ts_gst);
    trace!("Produced buffer {:?}", buffer);
    if video_state.frame_counter == 0 {
        video_state.frame_counter += 2;
    } else {
        video_state.frame_counter += 1;
    }
    Ok(CreateSuccess::NewBuffer(buffer))
}
