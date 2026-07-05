// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::mxlsink::{self, state::AudioState};

use gstreamer::{self as gst, prelude::ElementExt};
use mxl::Rational;
use tracing::trace;

pub(crate) fn audio(
    state: &mut mxlsink::state::State,
    element: &gst::Element,
    buffer: &gst::Buffer,
    offset: u64,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let base_time = element.base_time().ok_or(gst::FlowError::Error)?;
    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let src = map.as_slice();
    let buffer_length = state
        .flow
        .as_ref()
        .ok_or(gst::FlowError::Error)?
        .continuous()
        .map_err(|_| gst::FlowError::Error)?
        .bufferLength as u64;
    let max_chunk = (buffer_length / 2) as usize;
    let gst_pts = buffer.pts().ok_or(gst::FlowError::Error)?;
    trace!("AUDIO gst PTS: {:#?}", gst_pts);

    let audio_state = state.audio.as_mut().ok_or(gst::FlowError::Error)?;
    let bytes_per_sample = (audio_state.flow_def.bit_depth / 8) as usize;
    let num_channels = audio_state.flow_def.channel_count as usize;
    let samples_per_buffer = src.len() / (num_channels * bytes_per_sample);
    let sample_rate = Rational {
        numerator: audio_state.flow_def.sample_rate.numerator as i64,
        denominator: audio_state.flow_def.sample_rate.denominator as i64,
    };

    let mut remaining = samples_per_buffer;
    let mut src_offset_samples = 0;
    // First chunk's MXL timestamp; each chunk advances by its own duration so
    // consecutive chunks map to consecutive sample indices.
    let mut base_mxl_ts = gst_pts
        .nseconds()
        .saturating_add(base_time.nseconds())
        .saturating_add(offset);

    while remaining > 0 {
        let chunk_mxl_ts = base_mxl_ts;
        let chunk_samples = remaining.min(max_chunk);
        let chunk_bytes = chunk_samples * num_channels * bytes_per_sample;
        let chunk = compute_chunk(
            src,
            bytes_per_sample,
            num_channels,
            src_offset_samples,
            chunk_bytes,
        );
        let chunk_duration_ns =
            (chunk_samples as u128 * sample_rate.denominator as u128 * 1_000_000_000u128)
                / sample_rate.numerator as u128;
        base_mxl_ts += chunk_duration_ns as u64;
        trace!(
            "AUDIO chunk with samples {:#?} with MXL ts: {:#?}",
            chunk_samples, chunk_mxl_ts
        );

        let mxl_index = state
            .instance
            .timestamp_to_index(chunk_mxl_ts, &sample_rate)
            .map_err(|_| gst::FlowError::Error)?;
        trace!("AUDIO mapped mxl_index from pts: {:#?}", mxl_index);

        // GstBaseSink (sync=true) has already waited for this buffer's running
        // time, so commit straight to the ring here: no separate pacing.
        commit_chunk(
            audio_state,
            mxl_index,
            chunk,
            chunk_samples,
            bytes_per_sample,
            num_channels,
        )?;
        src_offset_samples += chunk_samples;
        remaining -= chunk_samples;
    }
    Ok(gst::FlowSuccess::Ok)
}

fn commit_chunk(
    audio_state: &mut AudioState,
    index: u64,
    chunk: &[u8],
    chunk_samples: usize,
    bytes_per_sample: usize,
    num_channels: usize,
) -> Result<(), gst::FlowError> {
    let mut access = audio_state
        .writer
        .open_samples(index, chunk_samples)
        .map_err(|_| gst::FlowError::Error)?;
    write_samples_per_channel(
        bytes_per_sample,
        num_channels,
        &mut access,
        chunk_samples,
        chunk,
    )?;
    access.commit().map_err(|_| gst::FlowError::Error)?;
    Ok(())
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
