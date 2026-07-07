// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Standalone checks for the aligned A/V test sources, before any MXL round-trip
//! is involved. `videoconvert`/`audioconvert` act as ground truth: they decode
//! the native v210/UYVP and S24BE/S16BE packings, so a bad packer shows up as a
//! misplaced bar or a missing pip.
//!
//! The core property: at every pip instant the audio energy peaks *and* the
//! video bar is at screen centre. Both elements derive content from running
//! time (segment start 0, so `running_time == pts`), so the two sources are
//! compared on the same absolute timeline even when run in separate pipelines.

use std::sync::Once;

use gstreamer as gst;
use gstreamer_app as gst_app;
use gstreamer_audio as gst_audio;
use gstreamer_video as gst_video;
use gstreamer_video::prelude::*;

const PIP_INTERVAL_NS: u64 = 200_000_000; // 200 ms
const FPS: i32 = 25; // integer fps -> an exact frame sits on every pip
const WIDTH: i32 = 192;
const HEIGHT: i32 = 4;
const VIDEO_FRAMES: i32 = 27; // ~1.08 s
const RATE: i32 = 48_000;
const SAMPLES_PER_BUFFER: i32 = 1_024;
const AUDIO_BUFFERS: i32 = 50; // ~1.07 s

fn init() {
    static ONCE: Once = Once::new();
    ONCE.call_once(|| {
        gst::init().unwrap();
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

fn pull_all(sink: &gst_app::AppSink) -> Vec<gst::Sample> {
    let mut out = Vec::new();
    while let Ok(sample) = sink.pull_sample() {
        out.push(sample);
    }
    out
}

fn appsink(pipeline: &gst::Pipeline) -> gst_app::AppSink {
    pipeline
        .by_name("sink")
        .unwrap()
        .downcast::<gst_app::AppSink>()
        .unwrap()
}

fn run(desc: &str) -> Vec<gst::Sample> {
    let pipeline = gst::parse::launch(desc)
        .unwrap()
        .downcast::<gst::Pipeline>()
        .unwrap();
    pipeline.set_state(gst::State::Playing).unwrap();
    let samples = pull_all(&appsink(&pipeline));
    pipeline.set_state(gst::State::Null).unwrap();
    samples
}

fn argmax_column(sample: &gst::Sample) -> usize {
    let caps = sample.caps().unwrap();
    let info = gst_video::VideoInfo::from_caps(caps).unwrap();
    let buffer = sample.buffer().unwrap();
    let frame = gst_video::VideoFrameRef::from_buffer_ref_readable(buffer, &info).unwrap();
    let stride = frame.plane_stride()[0] as usize;
    let row = &frame.plane_data(0).unwrap()[..stride];
    let width = info.width() as usize;
    row.iter()
        .take(width)
        .enumerate()
        .max_by_key(|&(_, &v)| v)
        .map(|(x, _)| x)
        .unwrap_or(0)
}

/// `(running_time_ns, amplitude)` for every mono F32LE sample in `samples`.
fn audio_envelope(samples: &[gst::Sample]) -> Vec<(u64, f64)> {
    let mut out = Vec::new();
    for sample in samples {
        let info = gst_audio::AudioInfo::from_caps(sample.caps().unwrap()).unwrap();
        let rate = info.rate() as u64;
        let buffer = sample.buffer().unwrap();
        let pts = buffer.pts().unwrap().nseconds();
        let map = buffer.map_readable().unwrap();
        let samples_f32: &[f32] = gstavsynctest::analyze::f32le_samples(map.as_slice());
        for (i, &v) in samples_f32.iter().enumerate() {
            let t = pts + i as u64 * gst::ClockTime::SECOND.nseconds() / rate;
            out.push((t, v as f64));
        }
    }
    out
}

/// Number of full pips in an audio run of `duration_ns` (the pip at t=0 is
/// skipped by the source).
fn expected_pips(duration_ns: u64) -> u64 {
    let half = gstavsynctest::signal::DEFAULT_PIP_DURATION.nseconds() / 2;
    (1..)
        .take_while(|k| k * PIP_INTERVAL_NS + half <= duration_ns)
        .count() as u64
}

fn video_desc(format: &str) -> String {
    format!(
        "avsyncvideotestsrc num-buffers={VIDEO_FRAMES} pip-interval={PIP_INTERVAL_NS} is-live=false \
           ! video/x-raw,format={format},width={WIDTH},height={HEIGHT},framerate={FPS}/1 \
           ! videoconvert \
           ! video/x-raw,format=GRAY8 \
           ! appsink name=sink sync=false"
    )
}

fn audio_desc(format: &str) -> String {
    format!(
        "avsyncaudiotestsrc num-buffers={AUDIO_BUFFERS} pip-interval={PIP_INTERVAL_NS} is-live=false \
           ! audio/x-raw,format={format},channels=1,rate={RATE} \
           ! audioconvert \
           ! audio/x-raw,format=F32LE,channels=1,rate={RATE} \
           ! appsink name=sink sync=false"
    )
}

/// The bar is at screen centre on every pip frame and sweeps across the width.
#[test]
fn video_bar_sweeps_and_centres_on_pip() {
    init();
    let centre = WIDTH as f64 / 2.0;
    for format in ["v210", "UYVP"] {
        let samples = run(&video_desc(format));
        assert_eq!(
            samples.len(),
            VIDEO_FRAMES as usize,
            "{format}: expected {VIDEO_FRAMES} frames"
        );

        let frame_period = gst::ClockTime::SECOND.nseconds() / FPS as u64;
        let frames_per_interval = PIP_INTERVAL_NS / frame_period;
        // Bar width is one frame's step; centre frame lands within half of that.
        let tol = (WIDTH as f64 / frames_per_interval as f64).max(2.0);

        let centroids: Vec<(u64, f64)> = samples
            .iter()
            .map(|s| {
                let pts = s.buffer().unwrap().pts().unwrap().nseconds();
                (
                    pts,
                    gstavsynctest::analyze::bar_centroid(s).expect("bar present"),
                )
            })
            .collect();

        // Every pip frame (pts a multiple of the interval, except 0) is centred.
        for (pts, c) in &centroids {
            if *pts != 0 && pts.is_multiple_of(PIP_INTERVAL_NS) {
                assert!(
                    (c - centre).abs() <= tol,
                    "{format}: bar off-centre at pip pts {pts}: centroid {c}, want ~{centre}"
                );
            }
        }

        // And the bar actually moves across most of the frame.
        let cols: Vec<usize> = samples.iter().map(argmax_column).collect();
        let spread = cols.iter().max().unwrap() - cols.iter().min().unwrap();
        assert!(
            spread as f64 >= WIDTH as f64 * 0.4,
            "{format}: bar barely moved (spread {spread}px over width {WIDTH})"
        );
    }
}

/// The video source attaches a phase-locked CEA-708 caption: TICK/TOCK on each
/// pip frame (alternating), a null CDP on every other frame. Round-tripped
/// through `cdp-types`/`cea708-types` straight off the ancillary meta (before any
/// `videoconvert`, which would drop it). 25 fps puts an exact frame on each pip.
#[test]
fn captions_tick_tock_on_pip_frames() {
    init();
    let desc = format!(
        "avsyncvideotestsrc num-buffers={VIDEO_FRAMES} pip-interval={PIP_INTERVAL_NS} is-live=false \
           ! video/x-raw,format=v210,width={WIDTH},height={HEIGHT},framerate={FPS}/1 \
           ! appsink name=sink sync=false"
    );
    let samples = run(&desc);
    assert_eq!(samples.len(), VIDEO_FRAMES as usize);
    let frame_period = gst::ClockTime::SECOND.nseconds() / FPS as u64;

    let mut parser = cdp_types::CDPParser::new();
    let (mut ticks, mut tocks) = (0, 0);
    for s in &samples {
        let buffer = s.buffer().unwrap();
        let pts = buffer.pts().unwrap().nseconds();
        let n = pts / frame_period;
        let cdp =
            gstavsynctest::analyze::caption_cdp_bytes(buffer).expect("caption ancillary present");
        let text = gstavsynctest::analyze::decode_caption(&mut parser, &cdp);
        let on_pip = pts != 0 && pts.is_multiple_of(PIP_INTERVAL_NS);
        match text {
            Some(t) if on_pip => {
                let want = if (pts / PIP_INTERVAL_NS) % 2 == 1 {
                    "TICK"
                } else {
                    "TOCK"
                };
                assert_eq!(t, want, "frame {n} (pts {pts}): wrong caption");
                if want == "TICK" {
                    ticks += 1;
                } else {
                    tocks += 1;
                }
            }
            Some(t) => panic!("frame {n} (pts {pts}): caption {t:?} off a pip frame"),
            None => assert!(!on_pip, "frame {n} (pts {pts}): pip frame with no caption"),
        }
    }
    assert!(
        ticks >= 2 && tocks >= 2,
        "too few captions: {ticks} ticks, {tocks} tocks"
    );
}

/// Every native audio format decodes to a pip whose energy is centred on each
/// pip instant.
#[test]
fn audio_pips_land_on_interval() {
    init();
    let duration_ns =
        AUDIO_BUFFERS as u64 * SAMPLES_PER_BUFFER as u64 * gst::ClockTime::SECOND.nseconds()
            / RATE as u64;
    let n_pips = expected_pips(duration_ns);
    assert!(n_pips >= 4, "test too short: only {n_pips} pips");
    let tol = gstavsynctest::signal::DEFAULT_PIP_DURATION.nseconds() / 4;

    for format in ["F32LE", "S24BE", "S16BE"] {
        let env = audio_envelope(&run(&audio_desc(format)));
        for k in 1..=n_pips {
            let centre = k * PIP_INTERVAL_NS;
            let lo = centre - PIP_INTERVAL_NS / 2;
            let hi = centre + PIP_INTERVAL_NS / 2;
            let mut energy = 0.0f64;
            let mut weighted = 0.0f64;
            for &(t, a) in &env {
                if t > lo && t <= hi {
                    let e = a * a;
                    energy += e;
                    weighted += t as f64 * e;
                }
            }
            assert!(energy > 0.0, "{format}: no pip energy near {centre} ns");
            let detected = (weighted / energy) as u64;
            assert!(
                detected.abs_diff(centre) <= tol,
                "{format}: pip {k} centroid {detected} ns off from {centre} ns (tol {tol})"
            );
        }
    }
}
