// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::mxlsink::{self, state::InitialTime};

use glib::subclass::types::ObjectSubclassExt;
use gst::{ClockTime, prelude::*};
use gstreamer as gst;
use tracing::trace;

pub(crate) fn video(
    mxlsink: &mxlsink::imp::MxlSink,
    state: &mut mxlsink::state::State,
    buffer: &gst::Buffer,
) -> Result<gst::FlowSuccess, gst::FlowError> {
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
    let gst_time = mxlsink
        .obj()
        .current_running_time()
        .ok_or(gst::FlowError::Error)?;
    let initial_info = state.initial_time.get_or_insert(InitialTime {
        mxl_to_gst_offset: ClockTime::from_nseconds(state.instance.get_time()) - gst_time,
    });
    let mut index = current_index;
    match buffer.pts() {
        Some(pts) => {
            let mxl_pts = pts + initial_info.mxl_to_gst_offset;
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
            if index > current_index && index - current_index > video_state.grain_count as u64 {
                index = current_index + video_state.grain_count as u64 - 1;
            }
            video_state.grain_index = index;
        }
        None => {
            video_state.grain_index = current_index;
        }
    }

    commit_buffer(buffer, video_state, index)?;
    video_state.grain_index += 1;
    trace!("END RENDER");
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
