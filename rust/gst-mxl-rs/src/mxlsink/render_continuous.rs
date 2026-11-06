// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use crate::mxlsink::{
    self,
    imp::CAT,
    state::{ContinuousState, FlowState},
};

use gstreamer as gst;
use mxl::Rational;
use tracing::trace;

#[derive(Debug, PartialEq, Eq)]
pub(crate) enum Placement {
    /// Write the buffer from `skip` onwards, starting at flow index
    /// `index`.
    Write {
        index: u64,
        skip: u64,
    },
    Drop,
}

/// Alignment limits for audio buffers, in samples.
#[derive(Debug, Clone, Copy)]
pub(crate) struct AlignmentLimits {
    /// Largest distance between a buffer's timestamp and the next index at
    /// which the buffer still follows on from the previous one.
    pub threshold: u64,
    /// How long timestamps must stay beyond `threshold` before the sink
    /// follows them. 0 follows every jump beyond `threshold` at once.
    pub discont_wait: u64,
}

/// Write position of an audio stream, updated as buffers are placed and
/// written.
#[derive(Debug, Default)]
pub(crate) struct SampleAlignment {
    /// Index one past the last committed sample; `None` before the first
    /// commit.
    pub next_index: Option<u64>,
    /// Index at which buffer timestamps first moved beyond the alignment
    /// threshold; `None` while they are within it.
    pub discont_start: Option<u64>,
}

impl SampleAlignment {
    /// Decide where to write a buffer of `samples` samples whose timestamp
    /// maps to flow index `ts_index`. A buffer flagged `discont` is never moved
    /// to follow on from the previous one.
    ///
    /// A buffer further than `limits.threshold` from `next_index` still
    /// follows on until timestamps have been that far out for
    /// `limits.discont_wait`, measured from `discont_start`. A jump of at
    /// least `limits.discont_wait` is followed at once.
    pub(crate) fn place(
        &mut self,
        ts_index: u64,
        samples: u64,
        discont: bool,
        limits: &AlignmentLimits,
    ) -> Placement {
        let Some(next_index) = self.next_index else {
            return Placement::Write {
                index: ts_index,
                skip: 0,
            };
        };
        let distance = ts_index.abs_diff(next_index);
        let resync = if discont {
            true
        } else if distance <= limits.threshold {
            self.discont_start = None;
            false
        } else if limits.discont_wait == 0 {
            true
        } else {
            match self.discont_start {
                None if distance >= limits.discont_wait => true,
                None => {
                    self.discont_start = Some(next_index);
                    false
                }
                Some(start) => ts_index.abs_diff(start) >= limits.discont_wait,
            }
        };
        if !resync {
            return Placement::Write {
                index: next_index,
                skip: 0,
            };
        }
        self.discont_start = None;
        if ts_index > next_index {
            return Placement::Write {
                index: ts_index,
                skip: 0,
            };
        }
        let overlap = next_index - ts_index;
        if overlap >= samples {
            Placement::Drop
        } else {
            Placement::Write {
                index: next_index,
                skip: overlap,
            }
        }
    }
}

pub(crate) fn continuous(
    state: &mut mxlsink::state::State,
    element: &gst::Element,
    buffer: &gst::Buffer,
    mxl_ts: u64,
    alignment_threshold_ns: u64,
    discont_wait_ns: u64,
) -> Result<gst::FlowSuccess, gst::FlowError> {
    let map = buffer.map_readable().map_err(|_| gst::FlowError::Error)?;
    let src = map.as_slice();
    let buffer_length = state
        .flow_config
        .as_ref()
        .ok_or(gst::FlowError::Error)?
        .continuous()
        .map_err(|_| gst::FlowError::Error)?
        .bufferLength as u64;
    let max_chunk = (buffer_length / 2) as usize;
    trace!("AUDIO mapped mxl timestamp: {:#?}", mxl_ts);

    let continuous_state = match state.flow_state.as_mut() {
        Some(FlowState::Continuous(continuous)) => continuous,
        _ => return Err(gst::FlowError::Error),
    };
    let bytes_per_sample = (continuous_state.flow_def.bit_depth / 8) as usize;
    let num_channels = continuous_state.flow_def.channel_count as usize;
    let samples_per_buffer = src.len() / (num_channels * bytes_per_sample);
    let sample_rate = Rational {
        numerator: continuous_state.flow_def.sample_rate.numerator as i64,
        denominator: continuous_state.flow_def.sample_rate.denominator as i64,
    };

    let ts_index = state
        .instance
        .timestamp_to_index(mxl_ts, &sample_rate)
        .map_err(|_| gst::FlowError::Error)?;
    let limits = AlignmentLimits {
        threshold: duration_samples(alignment_threshold_ns, &sample_rate)?,
        discont_wait: duration_samples(discont_wait_ns, &sample_rate)?,
    };
    let samples_u64 = u64::try_from(samples_per_buffer).map_err(|_| gst::FlowError::Error)?;
    let discont = buffer
        .flags()
        .intersects(gst::BufferFlags::DISCONT | gst::BufferFlags::RESYNC);
    let placement = continuous_state
        .alignment
        .place(ts_index, samples_u64, discont, &limits);
    trace!(
        "AUDIO buffer with timestamp index {} and expected index {:?}: {:?}",
        ts_index, continuous_state.alignment.next_index, placement
    );
    let (start_index, skip) = match placement {
        Placement::Write { index, skip } => (index, skip),
        Placement::Drop => {
            gst::warning!(
                CAT,
                obj = element,
                "Dropping audio buffer at index {} with {} samples.",
                ts_index,
                samples_per_buffer,
            );
            return Ok(gst::FlowSuccess::Ok);
        }
    };
    if skip > 0 {
        gst::warning!(
            CAT,
            obj = element,
            "Audio buffer at index {} overlaps already written samples, dropping its first {} samples",
            ts_index,
            skip
        );
    } else if !discont
        && continuous_state
            .alignment
            .next_index
            .is_some_and(|next| start_index != next)
    {
        gst::warning!(
            CAT,
            obj = element,
            "Discontinuity in audio timestamps: expected index {:?}, got {}",
            continuous_state.alignment.next_index,
            ts_index
        );
    }

    let skip = usize::try_from(skip).map_err(|_| gst::FlowError::Error)?;
    let mut remaining = samples_per_buffer - skip;
    let mut src_offset_samples = skip;
    let mut chunk_index = start_index;

    while remaining > 0 {
        let chunk_samples = remaining.min(max_chunk);
        let chunk_bytes = chunk_samples * num_channels * bytes_per_sample;
        let chunk = compute_chunk(
            src,
            bytes_per_sample,
            num_channels,
            src_offset_samples,
            chunk_bytes,
        );
        trace!(
            "AUDIO chunk with samples {:#?} at MXL index: {:#?}",
            chunk_samples, chunk_index
        );

        // GstBaseSink (sync=true) has already waited for this buffer's running
        // time, so commit straight to the ring here: no separate pacing.
        chunk_index = commit_chunk(
            element,
            continuous_state,
            chunk_index,
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

/// `duration_ns` in samples at `sample_rate`.
fn duration_samples(duration_ns: u64, sample_rate: &Rational) -> Result<u64, gst::FlowError> {
    let numerator = u128::try_from(sample_rate.numerator).map_err(|_| gst::FlowError::Error)?;
    let denominator = u128::try_from(sample_rate.denominator).map_err(|_| gst::FlowError::Error)?;
    if denominator == 0 {
        return Err(gst::FlowError::Error);
    }
    let samples = duration_ns as u128 * numerator / (denominator * 1_000_000_000u128);
    u64::try_from(samples).map_err(|_| gst::FlowError::Error)
}

/// Write `chunk` starting at flow index `index` and return the index one past
/// its last sample.
fn commit_chunk(
    element: &gst::Element,
    continuous_state: &mut ContinuousState,
    index: u64,
    chunk: &[u8],
    chunk_samples: usize,
    bytes_per_sample: usize,
    num_channels: usize,
) -> Result<u64, gst::FlowError> {
    // `open_samples(end, count)` writes the `count` samples at absolute indices
    // `[end - count, end)` (last written is `end - 1`). `index` is this chunk's
    // first sample, so pass `index + chunk_samples` as the end.
    let chunk_samples_u64 = u64::try_from(chunk_samples).map_err(|_| gst::FlowError::Error)?;
    let end = index
        .checked_add(chunk_samples_u64)
        .ok_or(gst::FlowError::Error)?;
    let mut access = continuous_state
        .writer
        .open_samples(end, chunk_samples)
        .map_err(|e| {
            gst::error!(
                CAT,
                obj = element,
                "Failed to open {} samples ending at index {} (next index {:?}): {}",
                chunk_samples,
                end,
                continuous_state.alignment.next_index,
                e
            );
            gst::FlowError::Error
        })?;
    write_samples_per_channel(
        bytes_per_sample,
        num_channels,
        &mut access,
        chunk_samples,
        chunk,
    )?;
    access.commit().map_err(|e| {
        gst::error!(
            CAT,
            obj = element,
            "Failed to commit samples ending at index {}: {}",
            end,
            e
        );
        gst::FlowError::Error
    })?;
    continuous_state.alignment.next_index = Some(end);
    Ok(end)
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
