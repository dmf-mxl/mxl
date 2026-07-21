// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

//! End-to-end audio/video synchronisation through MXL flows.
//!
//! `avsyncvideotestsrc` (a bar centred on every pip instant) and
//! `avsyncaudiotestsrc` (a tone pip on every pip instant) are phase-locked at
//! the source. Each is written to its own MXL flow and read back by an
//! independent `mxlsrc`; the test then checks the recovered audio pip still
//! lands on the recovered bar-centre video frame — i.e. A/V sync survives the
//! round-trip. The check is *relative* (pip running time vs the nearest video
//! frame's running time), so it does not depend on the consumer-side clock
//! offset, only on the two flows sharing one timeline.
//!
//! ```text
//! avsyncvideotestsrc ! v210 ! queue ! mxlsink (video flow)
//! avsyncaudiotestsrc ! F32LE ! queue ! mxlsink (audio flow)
//!
//! mxlsrc (video flow) ! queue ! appsink video_sink (sync=false)
//! mxlsrc (audio flow) ! queue ! appsink audio_sink (sync=false)
//! ```
//!
//! Requires Linux `/dev/shm` (tmpfs); the aligned test sources are registered
//! directly from the `gst-avsynctest-rs` dev-dependency.

#[macro_use]
mod common;

use std::sync::Once;

use common::{assert_bus_no_errors, collect_bus_errors, skip_reason};
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;

const FR_NUM: i32 = 50;
const FR_DEN: i32 = 1;
const FRAME_PERIOD_NS: u64 = gst::ClockTime::SECOND.nseconds() * FR_DEN as u64 / FR_NUM as u64;
const PIP_INTERVAL_NS: u64 = 200_000_000; // 200 ms
const FRAMES_PER_INTERVAL: u64 = PIP_INTERVAL_NS / FRAME_PERIOD_NS; // 10
const WIDTH: i32 = 192;
const HEIGHT: i32 = 4;
const NUM_FRAMES: i32 = 120; // < 256 so the byte-0 frame index is unambiguous
const RATE: i32 = 48_000;
const NUM_AUDIO_BUFFERS: i32 = 130; // covers the video run with margin
/// Frames a live reader may miss while attaching at the producer's head. Applied
/// once per reader: after attach the reader stays in the ring's history window
/// and drops nothing (in practice the miss is at most one or two frames).
const ATTACH_SLACK: usize = 5;

fn init() {
    common::init();
    static ONCE: Once = Once::new();
    ONCE.call_once(|| {
        gst::Element::register(
            None,
            "avsyncvideotestsrc",
            gst::Rank::NONE,
            gstavsynctest::videosrc::AvSyncVideoTestSrc::static_type(),
        )
        .unwrap();
        gst::Element::register(
            None,
            "avsyncaudiotestsrc",
            gst::Rank::NONE,
            gstavsynctest::audiosrc::AvSyncAudioTestSrc::static_type(),
        )
        .unwrap();
    });
}

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

fn appsink(pipeline: &gst::Pipeline, name: &str) -> gst_app::AppSink {
    pipeline
        .by_name(name)
        .unwrap_or_else(|| panic!("appsink {name}"))
        .downcast::<gst_app::AppSink>()
        .expect("AppSink downcast")
}

/// Running time of a sample's PTS in its segment.
fn running_time(sample: &gst::SampleRef) -> Option<gst::ClockTime> {
    let pts = sample.buffer()?.pts()?;
    sample
        .segment()?
        .downcast_ref::<gst::ClockTime>()?
        .to_running_time(pts)
}

/// Pull `(frame_idx, running_time_ns)` from the video appsink until the last
/// frame arrives or the deadline passes (a live pull races the real-time stream).
fn pull_video(sink: &gst_app::AppSink) -> Vec<(u8, u64)> {
    let last = (NUM_FRAMES - 1) as u8;
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(20);
    let timeout = gst::ClockTime::from_seconds(2);
    let mut out = Vec::new();
    let mut seen_last = false;
    while !seen_last && std::time::Instant::now() < deadline {
        let Some(sample) = sink.try_pull_sample(timeout) else {
            if sink.is_eos() {
                break;
            }
            continue;
        };
        let Some(rt) = running_time(&sample) else {
            continue;
        };
        let buffer = sample.buffer().expect("video buffer");
        let idx = buffer.map_readable().expect("video readable").as_slice()[0];
        seen_last = idx == last;
        out.push((idx, rt.nseconds()));
    }
    out
}

/// Drain `(running_time_ns, amplitude)` for every F32LE sample from the audio
/// appsink until `done` is set (the video capture has finished, which is past the
/// last bar-centre frame) and the sink has been flushed, or EOS/deadline. Draining
/// continuously — rather than exiting on an idle gap — avoids both back-pressuring
/// the audio `mxlsrc` and mistaking a transient gap for end-of-stream.
fn drain_audio(sink: &gst_app::AppSink, done: &std::sync::atomic::AtomicBool) -> Vec<(u64, f32)> {
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(20);
    let timeout = gst::ClockTime::from_mseconds(200);
    let mut out = Vec::new();
    while std::time::Instant::now() < deadline {
        let Some(sample) = sink.try_pull_sample(timeout) else {
            if sink.is_eos() || done.load(std::sync::atomic::Ordering::Relaxed) {
                break;
            }
            continue;
        };
        let Some(rt) = running_time(&sample) else {
            continue;
        };
        let base = rt.nseconds();
        let buffer = sample.buffer().expect("audio buffer");
        let map = buffer.map_readable().expect("audio readable");
        let body =
            gstavsynctest::analyze::f32le_samples(map.as_slice()).expect("F32LE buffer aligned");
        for (i, &v) in body.iter().enumerate() {
            let t = base + i as u64 * gst::ClockTime::SECOND.nseconds() / RATE as u64;
            out.push((t, v));
        }
    }
    out
}

fn build(video_flow: &str, audio_flow: &str, domain: &str, is_live: bool) -> RoundTrip {
    let producer_desc = format!(
        "avsyncvideotestsrc is-live={is_live} num-buffers={NUM_FRAMES} \
             pip-interval={PIP_INTERVAL_NS} width={WIDTH} height={HEIGHT} \
             framerate={FR_NUM}/{FR_DEN} \
           ! video/x-raw,format=v210,width={WIDTH},height={HEIGHT},framerate={FR_NUM}/{FR_DEN} \
           ! queue \
           ! mxlsink flow-id={video_flow} domain={domain} \
         avsyncaudiotestsrc is-live={is_live} num-buffers={NUM_AUDIO_BUFFERS} \
             pip-interval={PIP_INTERVAL_NS} \
           ! audio/x-raw,format=F32LE,channels=1,rate={RATE} \
           ! queue \
           ! mxlsink flow-id={audio_flow} domain={domain}"
    );
    let producer = gst::parse::launch(&producer_desc)
        .expect("parse producer")
        .downcast::<gst::Pipeline>()
        .expect("producer pipeline");

    let consumer_desc = format!(
        "mxlsrc video-flow-id={video_flow} domain={domain} \
           ! queue \
           ! appsink name=video_sink sync=false caps=video/x-raw,format=v210 \
         mxlsrc audio-flow-id={audio_flow} domain={domain} \
           ! queue \
           ! appsink name=audio_sink sync=false caps=audio/x-raw,format=F32LE"
    );
    let consumer = gst::parse::launch(&consumer_desc)
        .expect("parse consumer")
        .downcast::<gst::Pipeline>()
        .expect("consumer pipeline");

    RoundTrip { producer, consumer }
}

/// Every recovered audio pip that lands near a bar-centre video frame coincides
/// with it to within the video's frame quantization.
///
/// `mxlsink` snaps every buffer's PTS onto its flow's index grid (`timestamp_to_index`),
/// the only lossy step in the round-trip. For the discrete video flow that grid
/// is the frame period, so a bar-centre frame's recovered time can sit up to half
/// a frame from the exact pip instant; the continuous audio flow's 48 kHz grid is
/// effectively exact. The pip therefore lands within one frame period of its
/// bar-centre frame — that gap is inherent video quantization, not A/V drift, so
/// the check is a time tolerance rather than an exact frame-index match (which is
/// ±1 ambiguous right at the half-frame boundary).
///
/// The check iterates the bar-centre frames (not the pips): the audio source runs
/// a little past the video, so trailing pips have no bar-centre frame, and a live
/// reader may miss the first pip while attaching — frames outside the pip span are
/// skipped. Every bar-centre frame the audio spans must have a coincident pip: a
/// pip *near* the frame that is over one frame period off is an A/V sync error; a
/// *missing* pip (nearest is an adjacent pip ~one interval away) means the audio
/// round-trip dropped samples. Neither is tolerated.
fn assert_av_aligned(video: &[(u8, u64)], pips: &[u64]) {
    // The video reader attaches within ATTACH_SLACK of the head and then reads
    // contiguously through to the last frame: no duplicates, no mid-stream gaps
    // (a steady-state drop), and no early stop.
    let frames: std::collections::BTreeSet<u8> = video.iter().map(|(i, _)| *i).collect();
    assert_eq!(
        frames.len(),
        video.len(),
        "duplicate video frames in capture: {video:?}"
    );
    let first = *frames.iter().next().expect("no video frames") as usize;
    let last = *frames.iter().next_back().unwrap() as usize;
    assert_eq!(
        last,
        NUM_FRAMES as usize - 1,
        "video did not read through the last frame {}, got {last}",
        NUM_FRAMES - 1
    );
    assert!(
        first <= ATTACH_SLACK,
        "video missed too many frames at attach: first read frame_idx {first}"
    );
    assert_eq!(
        frames.len(),
        last - first + 1,
        "video capture has gaps: {} frames over range {first}..={last}",
        frames.len()
    );
    assert!(!pips.is_empty(), "no audio pips detected");
    let (first_pip, last_pip) = (*pips.first().unwrap(), *pips.last().unwrap());

    let mut in_span = 0;
    for &(idx, rt) in video
        .iter()
        .filter(|(i, _)| (*i as u64).is_multiple_of(FRAMES_PER_INTERVAL))
    {
        // Only frames the audio capture actually spans.
        if rt + FRAME_PERIOD_NS < first_pip || rt > last_pip + FRAME_PERIOD_NS {
            continue;
        }
        in_span += 1;
        let nearest = pips
            .iter()
            .copied()
            .min_by_key(|p| p.abs_diff(rt))
            .expect("pip present");
        let d = nearest.abs_diff(rt);
        assert!(
            d <= FRAME_PERIOD_NS,
            "bar-centre frame {idx} (rt {rt}): nearest pip {nearest} is {d} ns away \
             ({}) — pips: {pips:?}",
            if d < PIP_INTERVAL_NS / 2 {
                "over one frame period: A/V sync error"
            } else {
                "no pip near this frame: audio round-trip dropped samples"
            },
        );
    }

    let expected = (NUM_FRAMES as u64 / FRAMES_PER_INTERVAL) as usize;
    assert!(
        in_span >= expected - 2,
        "audio spanned only {in_span} of ~{expected} bar-centre frames (pips: {pips:?})",
    );
}

fn run_case(test: &str, is_live: bool) {
    init();
    const FACTORIES: &[&str] = &[
        "appsink",
        "avsyncaudiotestsrc",
        "avsyncvideotestsrc",
        "mxlsink",
        "mxlsrc",
        "queue",
    ];
    if let Some(reason) = skip_reason(FACTORIES) {
        skip!(reason);
    }

    let video_flow = uuid::Uuid::new_v4().to_string();
    let audio_flow = uuid::Uuid::new_v4().to_string();
    let domain_guard = common::TestDomainGuard::new(test);
    let domain = domain_guard.domain();

    let rt = build(&video_flow, &audio_flow, &domain, is_live);
    rt.producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    rt.consumer
        .set_state(gst::State::Playing)
        .expect("consumer Playing");
    let (res, _, _) = rt.consumer.state(gst::ClockTime::from_seconds(10));
    res.expect("consumer reached Playing");
    assert_bus_no_errors("consumer", &collect_bus_errors(&rt.consumer));

    // Drain both sinks concurrently: pulling video for ~2.4 s while the audio
    // appsink is unread would back-pressure the audio `mxlsrc` through its queue,
    // stall its ring reads, and force a catch-up skip (dropped pips).
    let video_sink = appsink(&rt.consumer, "video_sink");
    let audio_sink = appsink(&rt.consumer, "audio_sink");
    let done = std::sync::atomic::AtomicBool::new(false);
    let (video, audio) = std::thread::scope(|s| {
        let audio = s.spawn(|| drain_audio(&audio_sink, &done));
        let video = pull_video(&video_sink);
        done.store(true, std::sync::atomic::Ordering::Relaxed);
        (video, audio.join().expect("audio drain thread"))
    });
    assert_bus_no_errors("consumer", &collect_bus_errors(&rt.consumer));

    let pips = gstavsynctest::analyze::detect_pips(&audio);
    assert_av_aligned(&video, &pips);
    drop(rt);
}

/// Live producer: both sources pace themselves against the pipeline clock.
#[test]
fn av_sync_live_via_mxl() {
    run_case("av_sync_live", true);
}

/// Non-live producer: sources push as fast as possible; the sync=true mxlsinks
/// pace the commits.
#[test]
fn av_sync_non_live_via_mxl() {
    run_case("av_sync_non_live", false);
}
