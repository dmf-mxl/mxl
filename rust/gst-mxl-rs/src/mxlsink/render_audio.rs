// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::mxlsink;

use gstreamer::{self as gst};
use tracing::trace;

pub(crate) fn audio(
    state: &mut mxlsink::state::State,
    buffer: &gst::Buffer,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let src = map.as_slice();
    let audio_state = state.audio.as_mut().ok_or(gst::FlowError::Error)?;

    let bytes_per_sample = (audio_state.flow_def.bit_depth / 8) as usize;
    trace!(
        "received buffer size: {}, channel count: {}, bit-depth: {}, bytes-per-sample: {}",
        src.len(),
        audio_state.flow_def.channel_count,
        audio_state.bit_depth,
        bytes_per_sample
    );

    let samples_per_buffer =
        src.len() / (audio_state.flow_def.channel_count as usize * bytes_per_sample);
    audio_state.batch_size = samples_per_buffer;

    let flow = state.flow.as_ref().ok_or(gst::FlowError::Error)?;
    let flow_info = flow.continuous().map_err(|_| gst::FlowError::Error)?;
    let sample_rate = flow
        .common()
        .sample_rate()
        .map_err(|_| gst::FlowError::Error)?;
    let buffer_length = flow_info.bufferLength as u64;

    let mut write_index = match audio_state.next_write_index {
        Some(idx) => idx,
        None => {
            let current_index = state.instance.get_current_index(&sample_rate);
            audio_state.next_write_index = Some(current_index);
            current_index
        }
    };

    trace!(
        "Writing audio batch starting at index {}, sample_rate {}/{}",
        write_index, sample_rate.numerator, sample_rate.denominator
    );

    let max_chunk = (buffer_length / 2) as usize;
    let num_channels = audio_state.flow_def.channel_count as usize;
    let samples_total = samples_per_buffer;
    let mut remaining = samples_total;
    let mut src_offset_samples = 0;

    while remaining > 0 {
        let (chunk_samples, chunk_bytes, mut access, samples_per_channel) =
            compute_samples_per_channel(
                audio_state,
                bytes_per_sample,
                write_index,
                max_chunk,
                num_channels,
                remaining,
            )?;

        let src_chunk = compute_chunk(
            src,
            bytes_per_sample,
            num_channels,
            src_offset_samples,
            chunk_bytes,
        );

        write_samples_per_channel(
            bytes_per_sample,
            num_channels,
            &mut access,
            samples_per_channel,
            src_chunk,
        )?;

        access.commit().map_err(|_| gst::FlowError::Error)?;
        trace!(
            "Committed chunk: {} samples at index {} ({} bytes)",
            chunk_samples, write_index, chunk_bytes
        );

        write_index = write_index.wrapping_add(chunk_samples as u64);
        src_offset_samples += chunk_samples;
        remaining -= chunk_samples;
    }
    audio_state.next_write_index = Some(write_index);
    Ok(gst::FlowSuccess::Ok)
}

fn compute_samples_per_channel(
    audio_state: &mut mxlsink::state::AudioState,
    bytes_per_sample: usize,
    write_index: u64,
    max_chunk: usize,
    num_channels: usize,
    remaining: usize,
) -> Result<(usize, usize, mxl::SamplesWriteAccess<'_>, usize), gst::FlowError> {
    let chunk_samples = remaining.min(max_chunk);
    let chunk_bytes = chunk_samples * num_channels * bytes_per_sample;
    let access = audio_state
        .writer
        .open_samples(write_index, chunk_samples)
        .map_err(|_| gst::FlowError::Error)?;
    let samples_per_channel = chunk_samples;
    Ok((chunk_samples, chunk_bytes, access, samples_per_channel))
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
