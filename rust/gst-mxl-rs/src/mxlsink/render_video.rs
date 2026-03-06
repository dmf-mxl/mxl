// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::Duration;

use crate::mxlsink::{
    self,
    render_audio::WriteGrainData,
    state::{InitialTime, VideoCommand, VideoEngine},
};

use gstreamer::{
    self as gst,
    prelude::{ClockExt, ElementExt},
};
use mxl::Rational;
use tracing::trace;

const LATENCY_CUSHION: u64 = 5_000_000;

pub(crate) fn video(
    state: &mut mxlsink::state::State,
    buffer: &gst::Buffer,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let video_state = state.video.as_mut().ok_or(gst::FlowError::Error)?;
    let flow = state.flow.as_ref().ok_or(gst::FlowError::Error)?;
    let grain_rate = flow
        .common()
        .grain_rate()
        .map_err(|_| gst::FlowError::Error)?;

    let clock = state.pipeline.clock().ok_or(gst::FlowError::Error)?;
    let gst_now = clock.time();
    let mxl_now = state.instance.get_time();
    let initial = video_state.initial_time.get_or_insert(InitialTime {
        mxl_pts_offset: mxl_now - gst_now.nseconds(),
    });

    let initial_pts_offset = initial.mxl_pts_offset;

    let gst_pts = buffer.pts().ok_or(gst::FlowError::Error)?;
    let mxl_pts = gst_pts.nseconds() + initial_pts_offset;
    let mut pts = mxl_pts + grains_to_ns(video_state.latency, &grain_rate);
    if pts < mxl_now {
        let diff_ns = mxl_now - pts;
        let diff_grains = ns_to_grains(diff_ns, &grain_rate);
        video_state.latency += diff_grains;
        let latency_ns = grains_to_ns(video_state.latency, &grain_rate);
        pts = mxl_pts + latency_ns + LATENCY_CUSHION;
    }
    trace!("VIDEO gst PTS: {:#?}", gst_pts);
    trace!("VIDEO mapped PTS: {:#?}", pts);
    let mxl_index = state
        .instance
        .timestamp_to_index(pts, &grain_rate)
        .map_err(|_| gst::FlowError::Error)?;
    trace!("VIDEO mapped mxl_index from pts: {:#?}", mxl_index);
    commit_buffer(buffer, video_state, mxl_index)?;
    video_state.grain_index = video_state.grain_index.wrapping_add(1);

    Ok(gst::FlowSuccess::Ok)
}

fn commit_buffer(
    buffer: &gst::Buffer,
    video_state: &mut mxlsink::state::VideoState,
    index: u64,
) -> Result<(), gst::FlowError> {
    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let buf = map.to_vec();
    let data = WriteGrainData { buf, index };
    video_state
        .tx
        .send(VideoCommand::Write { data })
        .map_err(|_| gst::FlowError::Error)?;
    Ok(())
}

pub fn await_video_buffer(
    engine: &mut VideoEngine,
    rx: crossbeam::channel::Receiver<VideoCommand>,
) -> Result<(), gst::FlowError> {
    while let Ok(cmd) = rx.recv() {
        match cmd {
            VideoCommand::Write { data } => {
                if let Err(e) = write_buffer(engine, data) {
                    trace!("Video engine error: {:?}", e);
                }
            }
        }
    }
    trace!("DELETING VIDEO FLOW");
    engine
        .writer
        .take()
        .ok_or(gst::FlowError::Error)?
        .destroy()
        .map_err(|_| gst::FlowError::Error)?;
    Ok(())
}

fn write_buffer(engine: &mut VideoEngine, data: WriteGrainData) -> Result<(), gst::FlowError> {
    trace!("VIDEO THREAD IS WRITING A BUFFER");
    let mut access = engine
        .writer
        .as_ref()
        .ok_or(gst::FlowError::Error)?
        .open_grain(data.index)
        .map_err(|_| gst::FlowError::Error)?;
    let payload = access.payload_mut();
    let copy_len = std::cmp::min(payload.len(), data.buf.len());
    payload[..copy_len].copy_from_slice(&data.buf[..copy_len]);
    let total_slices = access.total_slices();
    let mxl_now = engine.instance.get_time();
    let pts = engine
        .instance
        .index_to_timestamp(data.index, &engine.grain_rate)
        .map_err(|_| gst::FlowError::Error)?;

    if pts > mxl_now {
        trace!("Video sleeping: {:#?}", Duration::from_nanos(pts - mxl_now));
        let (lock, cvar) = &*engine.sleep_flag;

        let shutdown_guard = lock.lock().map_err(|_| gst::FlowError::Error)?;

        let mut shutdown_guard = shutdown_guard;

        while !*shutdown_guard && pts > mxl_now {
            let remaining = Duration::from_nanos(pts - mxl_now);

            let (guard, timeout_result) = cvar
                .wait_timeout(shutdown_guard, remaining)
                .map_err(|_| gst::FlowError::Error)?;

            shutdown_guard = guard;

            if timeout_result.timed_out() {
                break;
            }
        }

        if *shutdown_guard {
            return Ok(());
        }
    }
    access
        .commit(total_slices)
        .map_err(|_| gst::FlowError::Error)?;
    Ok(())
}

fn grains_to_ns(grains: u64, rate: &Rational) -> u64 {
    ((grains as u128 * 1_000_000_000u128 * rate.denominator as u128) / rate.numerator as u128)
        as u64
}

fn ns_to_grains(ns: u64, rate: &Rational) -> u64 {
    ((ns as u128 * rate.numerator as u128) / (rate.denominator as u128 * 1_000_000_000u128)) as u64
}
