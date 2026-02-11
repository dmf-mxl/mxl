///! Audio Buffer Rendering for MXL Sink
///!
///! This module handles the conversion of GStreamer audio buffers to MXL samples
///! and writes them to the MXL shared-memory ring buffer.
///!
///! ## Audio Format
///! - Input: GStreamer buffer with interleaved F32LE (32-bit float, little-endian)
///! - Output: MXL planar audio (separate channel planes)
///!
///! ## Ring Buffer Management
///! MXL uses a continuous (non-discrete) ring buffer for audio. The renderer:
///! 1. Calculates the write position based on sample rate and current time
///! 2. Splits large buffers into chunks (respecting max_chunk size)
///! 3. De-interleaves audio from GStreamer's interleaved format to MXL's planar format
///! 4. Handles ring buffer wraparound (two-plane access per channel)
///!
///! ## Synchronization
///! The write index advances based on the actual number of samples written,
///! keeping the writer synchronized with the MXL timeline.

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::mxlsink;

use gstreamer::{self as gst};
use tracing::trace;

/// Renders a GStreamer audio buffer to the MXL samples writer.
///
/// Converts interleaved audio samples to planar format and writes them to
/// the MXL ring buffer, handling chunking and wraparound.
///
/// # Arguments
/// * `state` - Element state containing audio writer and flow configuration
/// * `buffer` - GStreamer buffer with interleaved F32LE audio samples
///
/// # Returns
/// * `Ok(FlowSuccess::Ok)` if samples were written successfully
/// * `Err(FlowError)` if writing failed or state is invalid
///
/// # Buffer Processing
/// 1. Map buffer as readable to access sample data
/// 2. Calculate number of samples and chunk size
/// 3. Split into chunks if buffer exceeds max_chunk
/// 4. De-interleave each chunk (interleaved -> planar)
/// 5. Commit chunks and advance write index
pub(crate) fn audio(
    state: &mut mxlsink::state::State,
    buffer: &gst::Buffer,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    // Map buffer for read-only access to sample data
    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let src = map.as_slice();
    let audio_state = state.audio.as_mut().ok_or(gst::FlowError::Error)?;

    // Calculate bytes per sample and validate buffer size
    let bytes_per_sample = (audio_state.flow_def.bit_depth / 8) as usize;
    trace!(
        "received buffer size: {}, channel count: {}, bit-depth: {}, bytes-per-sample: {}",
        src.len(),
        audio_state.flow_def.channel_count,
        audio_state.bit_depth,
        bytes_per_sample
    );

    // Calculate total samples per channel in this buffer
    let samples_per_buffer =
        src.len() / (audio_state.flow_def.channel_count as usize * bytes_per_sample);
    audio_state.batch_size = samples_per_buffer;

    // Get flow configuration for ring buffer parameters
    let flow = state.flow.as_ref().ok_or(gst::FlowError::Error)?;
    let flow_info = flow.continuous().map_err(|_| gst::FlowError::Error)?;
    let sample_rate = flow
        .common()
        .sample_rate()
        .map_err(|_| gst::FlowError::Error)?;
    let buffer_length = flow_info.bufferLength as u64;

    // Determine write position in the ring buffer
    let mut write_index = match audio_state.next_write_index {
        Some(idx) => idx,
        None => {
            // First write: synchronize with MXL timeline
            let current_index = state.instance.get_current_index(&sample_rate);
            audio_state.next_write_index = Some(current_index);
            current_index
        }
    };

    trace!(
        "Writing audio batch starting at index {}, sample_rate {}/{}",
        write_index, sample_rate.numerator, sample_rate.denominator
    );

    // Calculate maximum chunk size (half the ring buffer to avoid wraparound issues)
    let max_chunk = (buffer_length / 2) as usize;
    let num_channels = audio_state.flow_def.channel_count as usize;
    let samples_total = samples_per_buffer;
    let mut remaining = samples_total;
    let mut src_offset_samples = 0;

    // Write buffer in chunks (handles buffers larger than max_chunk)
    while remaining > 0 {
        // Open write access for this chunk in the ring buffer
        let (chunk_samples, chunk_bytes, mut access, samples_per_channel) =
            compute_samples_per_channel(
                audio_state,
                bytes_per_sample,
                write_index,
                max_chunk,
                num_channels,
                remaining,
            )?;

        // Extract the relevant chunk from the source buffer
        let src_chunk = compute_chunk(
            src,
            bytes_per_sample,
            num_channels,
            src_offset_samples,
            chunk_bytes,
        );

        // De-interleave and write samples to MXL's planar format
        write_samples_per_channel(
            bytes_per_sample,
            num_channels,
            &mut access,
            samples_per_channel,
            src_chunk,
        )?;

        // Commit the write (makes data visible to readers)
        access.commit().map_err(|_| gst::FlowError::Error)?;
        trace!(
            "Committed chunk: {} samples at index {} ({} bytes)",
            chunk_samples, write_index, chunk_bytes
        );

        // Advance to next chunk (wraparound is handled by MXL)
        write_index = write_index.wrapping_add(chunk_samples as u64);
        src_offset_samples += chunk_samples;
        remaining -= chunk_samples;
    }

    // Update write index for next buffer
    audio_state.next_write_index = Some(write_index);
    Ok(gst::FlowSuccess::Ok)
}

/// Computes chunk parameters and opens write access to the MXL ring buffer.
///
/// # Arguments
/// * `audio_state` - Audio state with samples writer
/// * `bytes_per_sample` - Bytes per audio sample (typically 4 for F32LE)
/// * `write_index` - Ring buffer index to write at
/// * `max_chunk` - Maximum samples per chunk (ring buffer constraint)
/// * `num_channels` - Number of audio channels
/// * `remaining` - Samples remaining to write
///
/// # Returns
/// Tuple of (chunk_samples, chunk_bytes, write_access, samples_per_channel)
fn compute_samples_per_channel(
    audio_state: &mut mxlsink::state::AudioState,
    bytes_per_sample: usize,
    write_index: u64,
    max_chunk: usize,
    num_channels: usize,
    remaining: usize,
) -> Result<(usize, usize, mxl::SamplesWriteAccess<'_>, usize), gst::FlowError> {
    // Limit chunk to smaller of remaining samples or max_chunk
    let chunk_samples = remaining.min(max_chunk);
    let chunk_bytes = chunk_samples * num_channels * bytes_per_sample;

    // Open write access to the ring buffer at the specified index
    let access = audio_state
        .writer
        .open_samples(write_index, chunk_samples)
        .map_err(|_| gst::FlowError::Error)?;

    let samples_per_channel = chunk_samples;
    Ok((chunk_samples, chunk_bytes, access, samples_per_channel))
}

/// Writes de-interleaved audio samples to MXL's planar format.
///
/// Converts GStreamer's interleaved layout (S1C1, S1C2, S2C1, S2C2...)
/// to MXL's planar layout (all C1 samples, all C2 samples...).
///
/// # Arguments
/// * `bytes_per_sample` - Size of each sample in bytes
/// * `num_channels` - Number of audio channels
/// * `access` - MXL write access (provides mutable channel planes)
/// * `samples_per_channel` - Number of samples to write per channel
/// * `src_chunk` - Source interleaved audio data
///
/// # Ring Buffer Wraparound
/// Each channel has two planes (plane1, plane2) to handle ring buffer wraparound.
/// The function writes to plane1 until full, then continues in plane2.
fn write_samples_per_channel(
    bytes_per_sample: usize,
    num_channels: usize,
    access: &mut mxl::SamplesWriteAccess<'_>,
    samples_per_channel: usize,
    src_chunk: &[u8],
) -> Result<(), gst::FlowError> {
    // Process each channel separately (de-interleaving)
    for ch in 0..num_channels {
        // Get mutable access to this channel's two planes (for ring wraparound)
        let (plane1, plane2) = access
            .channel_data_mut(ch)
            .map_err(|_| gst::FlowError::Error)?;

        let mut written = 0;
        let offset = ch * bytes_per_sample;  // Offset for this channel in interleaved data

        // Write all samples for this channel
        for i in 0..samples_per_channel {
            // Calculate position of this sample in the interleaved source
            let sample_offset = i * num_channels * bytes_per_sample + offset;
            if sample_offset + bytes_per_sample > src_chunk.len() {
                break;
            }

            // Write to plane1 if there's room, otherwise plane2 (ring wraparound)
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

/// Writes a single sample to a plane.
///
/// Copies `bytes_per_sample` bytes from the source chunk to the destination plane.
fn write_sample(
    bytes_per_sample: usize,
    src_chunk: &[u8],
    plane1: &mut [u8],
    written: usize,
    sample_offset: usize,
) {
    // Copy sample bytes from interleaved source to planar destination
    plane1[written..written + bytes_per_sample]
        .copy_from_slice(&src_chunk[sample_offset..sample_offset + bytes_per_sample]);
}

/// Checks if a sample fits in the plane at the given offset.
///
/// Used to determine when to switch from plane1 to plane2 during wraparound.
fn does_sample_fit_in_plane(bytes_per_sample: usize, plane: &mut [u8], offset: usize) -> bool {
    offset + bytes_per_sample <= plane.len()
}

/// Extracts a chunk from the source buffer.
///
/// Calculates the byte range for the current chunk based on sample offset
/// and chunk size.
///
/// # Arguments
/// * `src` - Full source buffer
/// * `bytes_per_sample` - Size of each sample
/// * `num_channels` - Number of channels (for stride calculation)
/// * `src_offset_samples` - Sample offset (not byte offset)
/// * `chunk_bytes` - Total bytes in this chunk
///
/// # Returns
/// Slice of the source buffer containing the chunk
fn compute_chunk(
    src: &[u8],
    bytes_per_sample: usize,
    num_channels: usize,
    src_offset_samples: usize,
    chunk_bytes: usize,
) -> &[u8] {
    // Calculate start byte offset (samples * channels * bytes_per_sample)
    let start_byte = src_offset_samples * num_channels * bytes_per_sample;

    // Return slice from start_byte to start_byte + chunk_bytes
    &src[start_byte..start_byte + chunk_bytes]
}
