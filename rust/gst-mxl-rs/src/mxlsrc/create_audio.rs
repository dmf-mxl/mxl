// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::{Duration, Instant};

use crate::mxlsrc::imp::{CreateState, MxlSrc};
use crate::mxlsrc::mxl_helper::pts_subtrahend;
use crate::mxlsrc::state::{AudioState, State};
use crate::mxlsrc::timing::pts_for_index;
use gst::{Buffer, ClockTime};
use gstreamer as gst;
use mxl::{FlowInfo, SamplesData};
use tracing::trace;

const GET_SAMPLE_TIMEOUT: Duration = Duration::from_secs(2);
const PRODUCER_TIMEOUT: Duration = Duration::from_millis(100);
const DEFAULT_BATCH_SIZE: u32 = 48;

pub(crate) fn create_audio(
    src: &MxlSrc,
    state: &mut State,
    offset: u64,
) -> Result<CreateState, gst::FlowError> {
    let subtrahend = pts_subtrahend(src, offset)?;
    let audio_state = state.audio.as_mut().ok_or(gst::FlowError::Error)?;

    let reader_info = audio_state
        .reader
        .get_info()
        .map_err(|_| gst::FlowError::Error)?;

    let continuous_flow_info = reader_info
        .config
        .continuous()
        .map_err(|_| gst::FlowError::Error)?;

    let sample_rate = reader_info
        .config
        .common()
        .sample_rate()
        .map_err(|_| gst::FlowError::Error)?;

    let batch_size = DEFAULT_BATCH_SIZE
        .min(continuous_flow_info.bufferLength / 2)
        .min(reader_info.config.common().max_commit_batch_size_hint());
    let ring = continuous_flow_info.bufferLength as u64;
    let batch = batch_size as u64;

    audio_state_init(batch, &reader_info, audio_state);

    let head = reader_info.runtime.head_index();
    wait_for_sample(head, batch, audio_state)?;

    if is_reader_late(head, batch, ring, audio_state)? {
        resync_state(audio_state);
    }

    // `get_samples(end, count)` returns the `count` samples at absolute indices
    // `[end - count, end)` (last is `end - 1`). `audio_state.index` is the first
    // sample we want, so pass `index + batch` as the end; `pts_for_index(index)`
    // then stamps that first returned sample.
    let read_once = |idx: u64| {
        audio_state
            .samples_reader
            .get_samples(idx + batch, batch as usize, GET_SAMPLE_TIMEOUT)
    };

    let samples = match read_once(audio_state.index) {
        Ok(s) => s,
        Err(_) => {
            return Ok(CreateState::NoDataCreated);
        }
    };

    //Right now audio is being interleaved but in the future audio will be planar.
    let interleaved = interleave_audio(&samples)?;

    let Some(pts) = pts_for_index(&state.instance, audio_state.index, &sample_rate, subtrahend)?
    else {
        // Samples before the pipeline base time map to a negative running time
        // (before the consumer joined); a live source must not emit them. Skip
        // this batch forward rather than clamp its PTS to 0. Readers share
        // `subtrahend`, so they skip the same samples and stay index-aligned.
        trace!(
            "Skipping pre-start sample batch {} (running time would be negative)",
            audio_state.index
        );
        audio_state.index += batch;
        return Ok(CreateState::NoDataCreated);
    };

    let is_discont = std::mem::take(&mut audio_state.next_discont);

    let buffer = build_buffer(pts, samples, is_discont, interleaved)?;

    audio_state.index += batch;

    trace!(
        "read_index={} buffer PTS: {:?}",
        audio_state.index.saturating_sub(batch),
        pts,
    );

    Ok(CreateState::DataCreated(buffer))
}

fn audio_state_init(batch: u64, reader_info: &FlowInfo, audio_state: &mut AudioState) {
    if !audio_state.is_initialized {
        audio_state.index = reader_info.runtime.head_index().saturating_sub(batch);
        audio_state.is_initialized = true;
    }
}

fn resync_state(audio_state: &mut AudioState) {
    audio_state.next_discont = true;
}

fn wait_for_sample(
    mut head: u64,
    batch: u64,
    audio_state: &AudioState,
) -> Result<(), gst::FlowError> {
    let start = Instant::now();
    while audio_state.index + batch > head {
        if start.elapsed() > PRODUCER_TIMEOUT {
            return Ok(());
        }
        head = wait_for_producer(head, batch, audio_state)?;
    }
    Ok(())
}

fn wait_for_producer(
    mut head: u64,
    batch: u64,
    audio_state: &AudioState,
) -> Result<u64, gst::FlowError> {
    trace!(
        "Reader ahead: index {} + batch {} > head {} (waiting for producer)",
        audio_state.index, batch, head
    );
    head = audio_state
        .reader
        .get_info()
        .map_err(|_| gst::FlowError::Error)?
        .runtime
        .head_index();
    Ok(head)
}

fn is_reader_late(
    head: u64,
    batch: u64,
    ring: u64,
    audio_state: &mut AudioState,
) -> Result<bool, gst::FlowError> {
    let oldest_valid = head.saturating_sub(ring.saturating_sub(batch));
    if audio_state.index < oldest_valid {
        catch_up(head, batch, audio_state, oldest_valid);
        Ok(true)
    } else {
        Ok(false)
    }
}

fn catch_up(head: u64, batch: u64, audio_state: &mut AudioState, oldest_valid: u64) {
    let target = define_cushion(head, batch);
    audio_state.index = target;
    trace!(
        "CATCH-UP (pre-read): index {} < oldest {}. Jumping -> {}, head={}",
        audio_state.index, oldest_valid, target, head
    );
}

fn define_cushion(head: u64, batch: u64) -> u64 {
    //Jump to (head - 2 × batch) to give reader headroom in case the producer has already advanced to avoid being immediately late again
    let cushion = batch.saturating_mul(2);

    head.saturating_sub(cushion)
}

fn build_buffer(
    pts: ClockTime,
    samples: SamplesData<'_>,
    next_discont: bool,
    interleaved: Vec<u8>,
) -> Result<Buffer, gst::FlowError> {
    let mut buf_size = 0;
    for i in 0..samples.num_of_channels() {
        let (a, b) = samples.channel_data(i).map_err(|_| gst::FlowError::Error)?;
        buf_size += a.len() + b.len();
    }

    let mut buffer = gst::Buffer::with_size(buf_size).map_err(|_| gst::FlowError::Error)?;

    {
        let buffer = buffer.get_mut().ok_or(gst::FlowError::Error)?;
        buffer.set_pts(pts);

        if next_discont {
            buffer.set_flags(gst::BufferFlags::DISCONT);
        }

        let mut map = buffer.map_writable().map_err(|_| gst::FlowError::Error)?;
        map.as_mut_slice().copy_from_slice(&interleaved);
    }
    Ok(buffer)
}

fn interleave_audio(samples: &SamplesData<'_>) -> Result<Vec<u8>, gst::FlowError> {
    let num_channels = samples.num_of_channels();
    let mut channels: Vec<Vec<u8>> = Vec::with_capacity(num_channels);
    let mut total_samples_per_channel = 0;

    for ch in 0..num_channels {
        let (data1, data2) = samples
            .channel_data(ch)
            .map_err(|_| gst::FlowError::Error)?;
        let mut combined = Vec::with_capacity(data1.len() + data2.len());
        combined.extend_from_slice(data1);
        combined.extend_from_slice(data2);
        total_samples_per_channel = combined.len() / std::mem::size_of::<f32>();
        channels.push(combined);
    }
    let mut interleaved =
        Vec::with_capacity(total_samples_per_channel * num_channels * std::mem::size_of::<f32>());
    for frame in 0..total_samples_per_channel {
        for chan in &channels {
            let offset = frame * std::mem::size_of::<f32>();
            interleaved.extend_from_slice(&chan[offset..offset + std::mem::size_of::<f32>()]);
        }
    }
    Ok(interleaved)
}

#[cfg(test)]
mod ring_tests {
    use super::define_cushion;

    #[test]
    fn define_cushion_leaves_two_batches_of_headroom() {
        assert_eq!(define_cushion(1000, 48), 1000 - 96);
        assert_eq!(define_cushion(10, 48), 0);
    }

    #[test]
    fn oldest_valid_sample_index_formula() {
        let ring = 480u64;
        let batch = 48u64;
        let head = 500u64;
        let oldest_valid = head.saturating_sub(ring.saturating_sub(batch));
        assert_eq!(oldest_valid, 68);
    }
}
