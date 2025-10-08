// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::Instant;

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
    let _ = state.initial_time.get_or_insert(InitialTime {
        index: current_index,
        gst_time,
    });
    let initial_info = state.initial_time.as_ref().ok_or(gst::FlowError::Error)?;
    let mut index = current_index;
    match buffer.pts() {
        Some(pts) => {
            let pts = pts + initial_info.gst_time;
            index = state
                .instance
                .timestamp_to_index(pts.nseconds(), &video_state.grain_rate)
                .map_err(|_| gst::FlowError::Error)?
                + initial_info.index;

            trace!(
                "PTS {:?} mapped to grain index {}, current index is {} and running time is {} delta= {}",
                pts,
                index,
                current_index,
                gst_time,
                if pts > gst_time {
                    pts - gst_time
                } else {
                    ClockTime::from_mseconds(0)
                }
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
    let mut copy_len = std::cmp::min(payload.len(), data.len());
    let commit_time = Instant::now();
    payload[..copy_len].copy_from_slice(&data[..copy_len]);
    if copy_len > access.total_slices() as usize {
        copy_len = access.total_slices() as usize;
    }
    access
        .commit(copy_len as u16)
        .map_err(|_| gst::FlowError::Error)?;
    trace!(
        "Commit time: {}us of grain: {}",
        commit_time.elapsed().as_micros(),
        index
    );
    Ok(())
}
