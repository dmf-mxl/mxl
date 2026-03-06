// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::Duration;

use crate::mxlsink::{
    self,
    state::{AudioCommand, AudioEngine, InitialTime},
};

use gstreamer::{
    self as gst,
    prelude::{ClockExt, ElementExt},
};
use mxl::Rational;
use tracing::trace;

pub struct WriteSampleData {
    chunk: Vec<u8>,
    num_channels: usize,
    bytes_per_sample: usize,
    chunk_samples: usize,
    index: u64,
}

pub struct WriteGrainData {
    pub buf: Vec<u8>,
    pub index: u64,
}

const LATENCY_CUSHION: u64 = 5_000_000;

pub(crate) fn audio(
    state: &mut mxlsink::state::State,
    buffer: &gst::Buffer,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let src = map.as_slice();
    let flow_info = state
        .flow
        .as_ref()
        .ok_or(gst::FlowError::Error)?
        .continuous()
        .map_err(|_| gst::FlowError::Error)?;
    let buffer_length = flow_info.bufferLength as u64;
    let max_chunk = (buffer_length / 2) as usize;
    let clock = state.pipeline.clock().ok_or(gst::FlowError::Error)?;
    let gst_now = clock.time();
    let audio_state = state.audio.as_mut().ok_or(gst::FlowError::Error)?;
    let bytes_per_sample = (audio_state.flow_def.bit_depth / 8) as usize;
    let samples_per_buffer =
        src.len() / (audio_state.flow_def.channel_count as usize * bytes_per_sample);
    let gst_pts = buffer.pts().ok_or(gst::FlowError::Error)?;
    trace!("GST BUFFER PTS: {:#?}", gst_pts);
    let mxl_now = state.instance.get_time();

    let initial = audio_state.initial_time.get_or_insert(InitialTime {
        mxl_pts_offset: mxl_now - gst_now.nseconds(),
    });

    let initial_pts_offset = initial.mxl_pts_offset;
    let sample_rate = Rational {
        numerator: audio_state.flow_def.sample_rate.numerator as i64,
        denominator: audio_state.flow_def.sample_rate.denominator as i64,
    };

    let num_channels = audio_state.flow_def.channel_count as usize;
    let mut remaining = samples_per_buffer;
    let mut src_offset_samples = 0;
    let mut base_pts = gst_pts.nseconds() + initial_pts_offset;

    while remaining > 0 {
        let mxl_pts = base_pts;
        let chunk_samples = remaining.min(max_chunk);
        let chunk_bytes = chunk_samples * num_channels * bytes_per_sample;

        let chunk = compute_chunk(
            &map,
            bytes_per_sample,
            num_channels,
            src_offset_samples,
            chunk_bytes,
        )
        .to_vec();
        let chunk_duration_ns =
            (chunk_samples as u128 * sample_rate.denominator as u128 * 1_000_000_000u128)
                / sample_rate.numerator as u128;

        base_pts += chunk_duration_ns as u64;
        trace!(
            "CHUNK WITH SAMPLES {:#?} WITH MXL PTS: {:#?}",
            samples_per_buffer, mxl_pts
        );

        let latency_ns = samples_to_ns(audio_state.latency, &sample_rate);
        let mut pts = mxl_pts + latency_ns;
        let mxl_now = state.instance.get_time();
        if pts < mxl_now {
            let diff_ns = mxl_now - pts;
            let diff_samples = ns_to_samples(diff_ns, &sample_rate);
            audio_state.latency += diff_samples;
            trace!("AUDIO Latency increased by {:#?} samples", diff_samples);
            let latency_ns = samples_to_ns(audio_state.latency, &sample_rate);
            pts = mxl_pts + latency_ns + LATENCY_CUSHION;
        }
        let mxl_index = state
            .instance
            .timestamp_to_index(pts, &sample_rate)
            .map_err(|_| gst::FlowError::Error)?;

        let data = WriteSampleData {
            chunk,
            bytes_per_sample,
            num_channels,
            chunk_samples,
            index: mxl_index,
        };
        audio_state
            .tx
            .send(AudioCommand::Write { data })
            .map_err(|_| gst::FlowError::Error)?;
        src_offset_samples += chunk_samples;
        remaining -= chunk_samples;
    }
    Ok(gst::FlowSuccess::Ok)
}

fn write_samples_per_channel(
    bytes_per_sample: usize,
    num_channels: usize,
    access: &mut mxl::SamplesWriteAccess<'_>,
    samples_per_channel: usize,
    src_chunk: &[u8],
) -> Result<(), gst::FlowError> {
    for ch in 0..num_channels {
        let (plane1, plane2) = access
            .channel_data_mut(ch)
            .map_err(|_| gst::FlowError::Error)?;

        let mut written = 0;
        let offset = ch * bytes_per_sample;

        for i in 0..samples_per_channel {
            let sample_offset = i * num_channels * bytes_per_sample + offset;
            if sample_offset + bytes_per_sample > src_chunk.len() {
                break;
            }

            if does_sample_fit_in_plane(bytes_per_sample, plane1, written) {
                write_sample(bytes_per_sample, src_chunk, plane1, written, sample_offset);
            } else if written < plane1.len() + plane2.len() {
                let plane2_offset = written.saturating_sub(plane1.len());
                if does_sample_fit_in_plane(bytes_per_sample, plane2, plane2_offset) {
                    write_sample(
                        bytes_per_sample,
                        src_chunk,
                        plane2,
                        plane2_offset,
                        sample_offset,
                    );
                }
            }

            written += bytes_per_sample;
        }
    }
    Ok(())
}

fn write_sample(
    bytes_per_sample: usize,
    src_chunk: &[u8],
    plane1: &mut [u8],
    written: usize,
    sample_offset: usize,
) {
    plane1[written..written + bytes_per_sample]
        .copy_from_slice(&src_chunk[sample_offset..sample_offset + bytes_per_sample]);
}

fn does_sample_fit_in_plane(bytes_per_sample: usize, plane: &mut [u8], offset: usize) -> bool {
    offset + bytes_per_sample <= plane.len()
}

fn compute_chunk(
    src: &[u8],
    bytes_per_sample: usize,
    num_channels: usize,
    src_offset_samples: usize,
    chunk_bytes: usize,
) -> &[u8] {
    &src[src_offset_samples * num_channels * bytes_per_sample
        ..src_offset_samples * num_channels * bytes_per_sample + chunk_bytes]
}

pub fn await_audio_buffer(
    engine: &mut AudioEngine,
    rx: crossbeam::channel::Receiver<AudioCommand>,
) -> Result<(), gst::FlowError> {
    while let Ok(cmd) = rx.recv() {
        match cmd {
            AudioCommand::Write { data } => {
                if let Err(e) = write_buffer(engine, data) {
                    trace!("Audio engine error: {:?}", e);
                }
            }
        }
    }
    trace!("DELETING AUDIO FLOW");
    engine
        .writer
        .take()
        .ok_or(gst::FlowError::Error)?
        .destroy()
        .map_err(|_| gst::FlowError::Error)?;
    Ok(())
}

fn write_buffer(engine: &mut AudioEngine, data: WriteSampleData) -> Result<(), gst::FlowError> {
    let writer = engine.writer.as_mut().ok_or(gst::FlowError::Error)?;
    let sample_rate = engine.sample_rate;
    let mxl_index = data.index;

    let pts = engine
        .instance
        .index_to_timestamp(mxl_index, &sample_rate)
        .map_err(|_| gst::FlowError::Error)?;
    let mxl_now = engine.instance.get_time();
    if pts > mxl_now {
        trace!("Audio sleeping: {:#?}", Duration::from_nanos(pts - mxl_now));
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

    let mut access = writer
        .open_samples(mxl_index, data.chunk_samples)
        .map_err(|_| gst::FlowError::Error)?;
    write_samples_per_channel(
        data.bytes_per_sample,
        data.num_channels,
        &mut access,
        data.chunk_samples,
        data.chunk.as_slice(),
    )?;
    access.commit().map_err(|_| gst::FlowError::Error)?;
    Ok(())
}

fn samples_to_ns(samples: u64, rate: &Rational) -> u64 {
    ((samples as u128 * 1_000_000_000u128 * rate.denominator as u128) / rate.numerator as u128)
        as u64
}

fn ns_to_samples(ns: u64, rate: &Rational) -> u64 {
    ((ns as u128 * rate.numerator as u128) / (rate.denominator as u128 * 1_000_000_000u128)) as u64
}
