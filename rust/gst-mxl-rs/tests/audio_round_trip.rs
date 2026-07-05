// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! GStreamer integration test for the MXL continuous audio flow path.
//!
//! A 440 Hz sine is written through `mxlsink` into a per-test MXL domain under
//! `/dev/shm` and read back through `mxlsrc`. The continuous path shares the
//! clock/offset/pacing machinery with the discrete video and data paths, so
//! this guards that the audio side survives those changes: the reader must
//! deliver monotonic-PTS F32LE samples whose spectrum still peaks at 440 Hz.

#[macro_use]
mod common;

use common::{init, skip_reason};
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;

const SAMPLE_RATE: u32 = 48_000;
const CHANNELS: u32 = 2;
const TONE_HZ: f64 = 440.0;
/// A frequency the tone has no energy at, for a signal-to-rest-of-band ratio.
const OFF_TONE_HZ: f64 = 3_000.0;
/// Mono samples to accumulate before running the spectral check (~0.34 s).
const MIN_MONO_SAMPLES: usize = 16_384;

/// Goertzel power of `samples` at `freq` (single-bin DFT magnitude squared).
fn goertzel_power(samples: &[f32], sample_rate: f64, freq: f64) -> f64 {
    let n = samples.len();
    if n == 0 {
        return 0.0;
    }
    let k = (0.5 + (n as f64 * freq / sample_rate)).floor();
    let w = 2.0 * std::f64::consts::PI * k / n as f64;
    let coeff = 2.0 * w.cos();
    let (mut s1, mut s2) = (0.0f64, 0.0f64);
    for &x in samples {
        let s0 = x as f64 + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    s1 * s1 + s2 * s2 - coeff * s1 * s2
}

/// Left channel of an interleaved F32LE stereo buffer.
fn left_channel(interleaved: &[u8]) -> Vec<f32> {
    interleaved
        .chunks_exact(std::mem::size_of::<f32>() * CHANNELS as usize)
        .map(|frame| f32::from_le_bytes(frame[..4].try_into().expect("4-byte f32")))
        .collect()
}

/// Owns both pipelines; stops the writer then the reader on drop (including on
/// panic) so no MXL worker thread outlives the domain dir.
struct RoundTrip {
    producer: gst::Pipeline,
    consumer: gst::Pipeline,
}

impl Drop for RoundTrip {
    fn drop(&mut self) {
        let _ = self.producer.set_state(gst::State::Null);
        let _ = self.consumer.set_state(gst::State::Null);
    }
}

/// F32LE sine → `mxlsink` → MXL → `mxlsrc` → `appsink`, spectrum-checked at 440 Hz.
#[test]
fn f32le_tone_round_trip_via_mxl() {
    init();
    const FACTORIES: &[&str] = &[
        "appsink",
        "audioconvert",
        "audiotestsrc",
        "capsfilter",
        "mxlsink",
        "mxlsrc",
        "queue",
    ];
    if let Some(reason) = skip_reason(FACTORIES) {
        skip!(reason);
    }

    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = common::TestDomainGuard::new("audio_round_trip");
    let domain = domain_guard.domain();

    // `audiotestsrc` defaults to S16LE; `audioconvert` + the capsfilter pin the
    // F32LE/interleaved/rate/channels that `mxlsink` accepts. num-buffers bounds
    // the run at ~3.2 s of audio (150 × 1024 samples). is-live=true makes it
    // produce in real time rather than dumping all buffers at once, modelling a
    // real audio source: mxlsink can only hold a not-yet-due grain, not un-burst
    // a backlog of already-due ones, so an instant burst would (under load) lap
    // the ring before the reader keeps up.
    let producer_desc = format!(
        "audiotestsrc wave=sine freq={TONE_HZ} num-buffers=150 samplesperbuffer=1024 is-live=true \
           ! audioconvert \
           ! audio/x-raw,format=F32LE,layout=interleaved,channels={CHANNELS},rate={SAMPLE_RATE} \
           ! queue \
           ! mxlsink flow-id={flow_id} domain={domain}"
    );
    let consumer_desc = format!(
        "mxlsrc audio-flow-id={flow_id} domain={domain} \
           ! queue \
           ! appsink name=sink sync=false"
    );

    let producer = gst::parse::launch(&producer_desc)
        .expect("parse producer")
        .downcast::<gst::Pipeline>()
        .expect("producer pipeline");
    let consumer = gst::parse::launch(&consumer_desc)
        .expect("parse consumer")
        .downcast::<gst::Pipeline>()
        .expect("consumer pipeline");
    let appsink = consumer
        .by_name("sink")
        .expect("appsink")
        .downcast::<gst_app::AppSink>()
        .expect("AppSink downcast");

    let rt = RoundTrip { producer, consumer };
    rt.producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    rt.consumer
        .set_state(gst::State::Playing)
        .expect("consumer Playing");

    let mut mono: Vec<f32> = Vec::with_capacity(MIN_MONO_SAMPLES * 2);
    let mut prev_pts: Option<gst::ClockTime> = None;
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(15);
    while mono.len() < MIN_MONO_SAMPLES && std::time::Instant::now() < deadline {
        let Some(sample) = appsink.try_pull_sample(gst::ClockTime::from_seconds(2)) else {
            if appsink.is_eos() {
                break;
            }
            continue;
        };
        let caps = sample.caps().expect("sample caps");
        let s = caps.structure(0).expect("caps structure");
        assert_eq!(s.name(), "audio/x-raw");
        assert_eq!(s.get::<String>("format").as_deref(), Ok("F32LE"));
        assert_eq!(s.get::<i32>("rate"), Ok(SAMPLE_RATE as i32));
        assert_eq!(s.get::<i32>("channels"), Ok(CHANNELS as i32));

        let buffer = sample.buffer().expect("sample buffer");
        let pts = buffer.pts().expect("audio PTS");
        if let Some(prev) = prev_pts {
            assert!(pts >= prev, "audio PTS went backwards: {prev:?} -> {pts:?}");
        }
        prev_pts = Some(pts);

        let map = buffer.map_readable().expect("map readable");
        mono.extend(left_channel(map.as_slice()));
    }

    assert_bus_no_errors(&rt);
    assert!(
        mono.len() >= MIN_MONO_SAMPLES,
        "collected only {} mono samples (< {MIN_MONO_SAMPLES}); mxlsrc did not deliver the tone",
        mono.len()
    );

    let tone = goertzel_power(&mono, SAMPLE_RATE as f64, TONE_HZ);
    let off = goertzel_power(&mono, SAMPLE_RATE as f64, OFF_TONE_HZ);
    assert!(
        tone > off * 100.0,
        "expected a strong {TONE_HZ} Hz peak; power({TONE_HZ})={tone:.3e} vs \
         power({OFF_TONE_HZ})={off:.3e}"
    );
}

fn assert_bus_no_errors(rt: &RoundTrip) {
    let errors = common::collect_bus_errors(&rt.consumer);
    common::assert_bus_no_errors("consumer", &errors);
    let errors = common::collect_bus_errors(&rt.producer);
    common::assert_bus_no_errors("producer", &errors);
}
