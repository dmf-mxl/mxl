//! Audio Buffer Creation for MXL Source
//!
//! This module handles reading audio samples from MXL's shared-memory ring buffer
//! and converting them to GStreamer buffers.
//!
//! ## Audio Format
//! - Input: MXL planar audio (separate channel planes in shared memory)
//! - Output: GStreamer buffer with interleaved F32LE audio
//!
//! ## Synchronization Strategy
//! 1. **Initialization**: On first read, establish offset between MXL timeline and GStreamer running time
//! 2. **Producer monitoring**: Wait briefly if reader is ahead of writer (no data available yet)
//! 3. **Late detection**: If reader falls too far behind, jump forward (catch-up)
//! 4. **Discontinuity flagging**: Set DISCONT flag when catching up
//!
//! ## Ring Buffer Management
//! - Reads in batches (DEFAULT_BATCH_SIZE samples per buffer)
//! - Handles wraparound (two-plane access per channel)
//! - Advances read index after each successful read

// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::time::{Duration, Instant};

use crate::mxlsrc::imp::{CreateState, MxlSrc};
use crate::mxlsrc::state::{AudioState, InitialTime, State};
use glib::subclass::types::ObjectSubclassExt;
use gst::{Buffer, ClockTime, prelude::*};
use gstreamer as gst;
use mxl::{FlowInfo, MxlInstance, Rational, SamplesData};
use tracing::trace;

/// Maximum time to wait for samples to become available
const GET_SAMPLE_TIMEOUT: Duration = Duration::from_secs(2);

/// Maximum time to wait for producer to advance head index
const PRODUCER_TIMEOUT: Duration = Duration::from_millis(100);

/// Default number of samples per buffer (affects latency vs. efficiency)
const DEFAULT_BATCH_SIZE: u32 = 48;

/// Creates a GStreamer audio buffer from MXL samples.
///
/// This is the main data path for audio. It reads a batch of samples from
/// the MXL ring buffer, interleaves them, and wraps them in a GStreamer buffer.
///
/// # Arguments
/// * `src` - The source element (provides access to pipeline clock)
/// * `state` - Element state containing audio reader and timing info
///
/// # Returns
/// * `Ok(CreateState::DataCreated(buffer))` if samples were read successfully
/// * `Ok(CreateState::NoDataCreated)` if flow is stale (needs re-initialization)
/// * `Err(FlowError)` if reading failed
///
/// # Synchronization
/// - Initializes timing on first buffer
/// - Waits for producer if reader is ahead
/// - Catches up if reader is too far behind
/// - Adjusts PTS if buffer would be late
pub(crate) fn create_audio(src: &MxlSrc, state: &mut State) -> Result<CreateState, gst::FlowError> {
    // Get audio state and flow configuration
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

    // Calculate batch size (limited by buffer size and max batch hint)
    let batch_size = DEFAULT_BATCH_SIZE
        .min(continuous_flow_info.bufferLength / 2)  // Max half the ring buffer
        .min(reader_info.config.common().max_commit_batch_size_hint());
    let ring = continuous_flow_info.bufferLength as u64;
    let batch = batch_size as u64;

    // Get current GStreamer running time
    let Some(ts_gst) = src.obj().current_running_time() else {
        return Err(gst::FlowError::Error);
    };

    // Initialize timing on first read
    audio_state_init(
        &mut state.initial_info,
        &state.instance,
        ts_gst,
        batch,
        &reader_info,
        audio_state,
    );

    // Check producer position and wait if necessary
    let head = reader_info.runtime.head_index();
    wait_for_sample(head, batch, audio_state)?;

    // Catch up if reader has fallen too far behind
    if is_reader_late(head, batch, ring, audio_state)? {
        resync_state(
            ts_gst,
            &mut state.initial_info,
            &state.instance,
            audio_state,
        );
    }

    // Closure to read samples from MXL
    let read_once = |idx: u64| {
        audio_state
            .samples_reader
            .get_samples(idx, batch as usize, GET_SAMPLE_TIMEOUT)
    };

    // Read samples (or signal stale flow if timeout)
    let samples = match read_once(audio_state.index) {
        Ok(s) => s,
        Err(_) => {
            return Ok(CreateState::NoDataCreated);
        }
    };

    // Convert from planar to interleaved format
    // NOTE: Future MXL versions may support planar output directly
    let interleaved = interleave_audio(&samples)?;

    // Calculate actual duration of this batch (accounting for wraparound)
    let read_batch_duration =
        compute_batch_duration(audio_state.index, batch, &state.instance, &sample_rate)?;

    // Advance MXL timeline reference
    state.initial_info.mxl_index = state
        .initial_info
        .mxl_index
        .saturating_add(read_batch_duration);

    // Calculate buffer PTS
    let pts = compute_pts(
        batch,
        sample_rate,
        audio_state,
        ts_gst,
        &mut state.initial_info,
    );

    // Check if discontinuity flag should be set
    let is_discont = std::mem::take(&mut audio_state.next_discont);

    // Build GStreamer buffer
    let buffer = build_buffer(pts, samples, is_discont, interleaved)?;

    // Advance counters
    audio_state.batch_counter += 1;
    audio_state.index += batch;

    trace!(
        "Initial time: {} buffer PTS: {:?} gst running time: {}",
        state.initial_info.gst_time, pts, ts_gst
    );

    Ok(CreateState::DataCreated(buffer))
}

/// Initializes audio state on first buffer.
///
/// Establishes the timeline offset between MXL and GStreamer.
fn audio_state_init(
    initial_info: &mut InitialTime,
    instance: &MxlInstance,
    ts_gst: ClockTime,
    batch: u64,
    reader_info: &FlowInfo,
    audio_state: &mut AudioState,
) {
    // Only initialize once
    if !audio_state.is_initialized {
        // Establish initial timeline offset
        *initial_info = InitialTime {
            mxl_index: instance.get_time(),
            gst_time: ts_gst,
        };

        // Start reading one batch behind head (gives buffer for producer to advance)
        audio_state.index = reader_info.runtime.head_index().saturating_sub(batch);
        audio_state.is_initialized = true;
        audio_state.batch_counter = 0;
    }
}

/// Resyncs state after detecting lateness.
///
/// Resets the timeline offset and flags next buffer as discontinuous.
fn resync_state(
    ts_gst: ClockTime,
    initial_info: &mut InitialTime,
    instance: &MxlInstance,
    audio_state: &mut AudioState,
) {
    initial_info.gst_time = ts_gst;
    initial_info.mxl_index = instance.get_time();
    audio_state.batch_counter = 0;
    audio_state.next_discont = true;
}

/// Waits briefly if reader is ahead of producer.
///
/// Returns immediately if data is available, or after PRODUCER_TIMEOUT.
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

/// Checks if reader has fallen too far behind producer.
///
/// Returns true if reader index is older than the oldest valid sample.
fn is_reader_late(
    head: u64,
    batch: u64,
    ring: u64,
    audio_state: &mut AudioState,
) -> Result<bool, gst::FlowError> {
    let oldest_valid = head.saturating_sub(ring.saturating_sub(batch));
    if audio_state.index < oldest_valid {
        catch_up(head, batch, ring, audio_state, oldest_valid);
        Ok(true)
    } else {
        Ok(false)
    }
}

fn catch_up(head: u64, batch: u64, ring: u64, audio_state: &mut AudioState, oldest_valid: u64) {
    let target = define_cushion(head, batch);
    audio_state.index = target;
    trace!(
        "CATCH-UP (pre-read): index {} < oldest {}. Jumping -> {}, head={}, ring={}",
        audio_state.index, oldest_valid, target, head, ring
    );
}

fn define_cushion(head: u64, batch: u64) -> u64 {
    //Jump to (head - 2 × batch) to give reader headroom in case the producer has already advanced to avoid being immediately late again
    let cushion = batch.saturating_mul(2);

    head.saturating_sub(cushion)
}

fn compute_pts(
    batch: u64,
    sample_rate: Rational,
    audio_state: &AudioState,
    ts_gst: ClockTime,
    initial_info: &mut InitialTime,
) -> ClockTime {
    let batch_duration_ns = (batch as u128 * 1_000_000_000u128) * sample_rate.denominator as u128
        / sample_rate.numerator as u128;

    let pts_ns = gst::ClockTime::from_nseconds(
        (audio_state.batch_counter as u128 * batch_duration_ns) as u64,
    );
    let mut pts = initial_info.gst_time + pts_ns;

    if pts < ts_gst {
        initial_info.gst_time += ts_gst - pts;
        pts = ts_gst;
    }
    pts
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

/// Converts MXL planar audio to GStreamer interleaved format.
///
/// MXL stores each channel separately (planar), GStreamer expects
/// samples interleaved (S1C1, S1C2, S2C1, S2C2...).
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

fn compute_batch_duration(
    index: u64,
    batch: u64,
    instance: &MxlInstance,
    sample_rate: &Rational,
) -> Result<u64, gst::FlowError> {
    let next_index = index + batch;
    let next_head_timestamp = instance
        .index_to_timestamp(next_index, sample_rate)
        .map_err(|_| gst::FlowError::Error)?;
    let read_head_timestamp = instance
        .index_to_timestamp(index, sample_rate)
        .map_err(|_| gst::FlowError::Error)?;
    let read_batch_duration = next_head_timestamp - read_head_timestamp;
    Ok(read_batch_duration)
}
