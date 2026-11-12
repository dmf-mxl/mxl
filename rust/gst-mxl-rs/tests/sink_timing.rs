// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Integration tests for where `mxlsink` writes grains and samples in MXL time
//! when the stream pauses, stops and resumes, starts from a file-style segment,
//! is relayed from `mxlsrc`, changes source, or runs at a fractional frame rate.

use std::thread;
use std::time::{Duration, Instant};

#[macro_use]
mod common;

use common::{TestDomainGuard, assert_bus_no_errors, collect_bus_errors, init, skip_reason};
use gst::glib;
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;
use gstreamer_video as gst_video;
use mxl::{GrainReader, MxlInstance, Rational, SamplesReader, config::get_mxl_so_path};

const VIDEO_RATE: Rational = Rational {
    numerator: 25,
    denominator: 1,
};
const FRAME: gst::ClockTime = gst::ClockTime::from_mseconds(40);
const AUDIO_RATE: Rational = Rational {
    numerator: 48_000,
    denominator: 1,
};
const AUDIO_CAPS: &str = "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=1,\
    channel-mask=(bitmask)0x1";
const SAMPLES_PER_BUFFER: u64 = 480;
const AUDIO_BUFFER: gst::ClockTime = gst::ClockTime::from_mseconds(10);
/// How long the tests pause the pipeline, or stop delivering buffers.
const GAP: gst::ClockTime = gst::ClockTime::from_mseconds(400);
/// Budget for the pipeline to reach a condition before the test fails.
const TIMEOUT: Duration = Duration::from_secs(5);

fn mxl_instance(domain: &str) -> MxlInstance {
    let api = mxl::load_api(get_mxl_so_path()).expect("load MXL API");
    MxlInstance::new(api, domain, "").expect("MXL instance")
}

fn launch(desc: &str) -> gst::Pipeline {
    gst::parse::launch(desc)
        .expect("parse pipeline")
        .downcast::<gst::Pipeline>()
        .expect("pipeline")
}

fn appsrc(pipeline: &gst::Pipeline) -> gst_app::AppSrc {
    pipeline
        .by_name("src")
        .expect("appsrc")
        .downcast::<gst_app::AppSrc>()
        .expect("AppSrc downcast")
}

fn video_caps(numerator: i32, denominator: i32) -> String {
    format!("video/x-raw,format=v210,width=2,height=2,framerate={numerator}/{denominator}")
}

fn frame_bytes(numerator: i32, denominator: i32) -> usize {
    gst_video::VideoInfo::builder(gst_video::VideoFormat::V210, 2, 2)
        .fps(gst::Fraction::new(numerator, denominator))
        .build()
        .expect("v210 VideoInfo")
        .size()
}

/// A v210 frame whose first payload byte is `marker`.
fn video_frame(
    bytes: usize,
    pts: gst::ClockTime,
    duration: gst::ClockTime,
    marker: u8,
) -> gst::Buffer {
    let mut buffer = gst::Buffer::with_size(bytes).expect("v210 buffer");
    {
        let b = buffer.get_mut().expect("buffer mut");
        b.set_pts(pts);
        b.set_duration(duration);
        b.map_writable().expect("buffer writable").as_mut_slice()[0] = marker;
    }
    buffer
}

fn audio_buffer(pts: gst::ClockTime) -> gst::Buffer {
    let payload: Vec<u8> = (0..SAMPLES_PER_BUFFER)
        .flat_map(|_| 0.25f32.to_le_bytes())
        .collect();
    let mut buffer = gst::Buffer::from_mut_slice(payload);
    {
        let b = buffer.get_mut().expect("buffer mut");
        b.set_pts(pts);
        b.set_duration(AUDIO_BUFFER);
    }
    buffer
}

fn push_frames(appsrc: &gst_app::AppSrc, frames: std::ops::Range<u64>, offset: gst::ClockTime) {
    let bytes = frame_bytes(25, 1);
    for i in frames {
        let pts = offset + FRAME * i;
        appsrc
            .push_buffer(video_frame(bytes, pts, FRAME, i as u8))
            .expect("push frame");
    }
}

fn push_audio(appsrc: &gst_app::AppSrc, buffers: std::ops::Range<u64>, offset: gst::ClockTime) {
    for i in buffers {
        appsrc
            .push_buffer(audio_buffer(offset + AUDIO_BUFFER * i))
            .expect("push audio");
    }
}

/// Buffers `sink` has rendered, from `GstBaseSink`'s `stats`.
fn rendered(sink: &gst::Element) -> u64 {
    sink.property::<gst::Structure>("stats")
        .get::<u64>("rendered")
        .expect("rendered count")
}

fn wait_rendered(sink: &gst::Element, count: u64) {
    let deadline = Instant::now() + TIMEOUT;
    while rendered(sink) < count {
        assert!(
            Instant::now() < deadline,
            "{} rendered {} of {count} buffers",
            sink.name(),
            rendered(sink)
        );
        thread::sleep(Duration::from_millis(1));
    }
}

/// Signal EOS and wait for the sink to render everything. Panics on a bus error.
fn finish(pipeline: &gst::Pipeline) {
    appsrc(pipeline).end_of_stream().expect("end of stream");
    let msg = pipeline
        .bus()
        .expect("bus")
        .timed_pop_filtered(
            gst::ClockTime::from_seconds(5),
            &[gst::MessageType::Eos, gst::MessageType::Error],
        )
        .expect("timed out waiting for EOS");
    if let gst::MessageView::Error(err) = msg.view() {
        panic!(
            "error from {:?}: {} ({:?})",
            err.src().map(|s| s.path_string()),
            err.error(),
            err.debug()
        );
    }
}

fn grain_reader(instance: &MxlInstance, flow_id: &str) -> GrainReader {
    instance
        .create_flow_reader(flow_id)
        .expect("flow reader")
        .to_grain_reader()
        .expect("grain reader")
}

fn samples_reader(instance: &MxlInstance, flow_id: &str) -> SamplesReader {
    instance
        .create_flow_reader(flow_id)
        .expect("flow reader")
        .to_samples_reader()
        .expect("samples reader")
}

fn grain_head(reader: &GrainReader) -> u64 {
    reader.get_runtime_info().expect("runtime info").headIndex
}

fn samples_head(reader: &SamplesReader) -> u64 {
    reader.get_runtime_info().expect("runtime info").headIndex
}

fn marker_at(reader: &GrainReader, index: u64) -> u8 {
    let grain = reader
        .get_grain_non_blocking(index)
        .unwrap_or_else(|e| panic!("grain {index} not readable: {e:?}"));
    assert_eq!(grain.index, index, "grain {index} was overwritten");
    grain.payload[0]
}

/// The grains up to `head` hold consecutive markers ending at `last`. A reader
/// sees one grain less than the flow's grain count, which is 200 ms of grains
/// by default: 4 at 25 fps, 3 at 24000/1001.
fn assert_consecutive_grains(reader: &GrainReader, head: u64, last: u8, count: u64) {
    for back in 0..count {
        assert_eq!(
            marker_at(reader, head - back),
            last - back as u8,
            "grain {} should hold frame {}",
            head - back,
            last - back as u8
        );
    }
}

fn assert_near(label: &str, value: i64, expected: i64, tolerance: i64) {
    assert!(
        (value - expected).abs() <= tolerance,
        "{label}: {value}, expected {expected} +/- {tolerance}"
    );
}

/// Pausing and resuming the pipeline must not error, and writing must resume
/// at MXL now, a pause's worth of grains after the frame before the pause.
#[test]
fn pause_and_resume_video_writes_at_mxl_now() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("pause_resume_video");
    let domain = domain_guard.domain();
    let flow_id = uuid::Uuid::new_v4().to_string();
    let pipeline = launch(&format!(
        "appsrc name=src format=time caps={} \
         ! mxlsink name=sink flow-id={flow_id} domain={domain}",
        video_caps(25, 1)
    ));
    let sink = pipeline.by_name("sink").expect("mxlsink");
    let instance = mxl_instance(&domain);

    pipeline.set_state(gst::State::Playing).expect("Playing");
    push_frames(&appsrc(&pipeline), 0..5, gst::ClockTime::ZERO);
    wait_rendered(&sink, 5);
    let reader = grain_reader(&instance, &flow_id);
    let before = grain_head(&reader);

    // Running time stops for as long as the pipeline is paused, which is
    // longer than GAP when the sleep overruns.
    let paused = Instant::now();
    pipeline.set_state(gst::State::Paused).expect("Paused");
    thread::sleep(GAP.into());
    pipeline.set_state(gst::State::Playing).expect("Playing");
    let paused_for = gst::ClockTime::try_from(paused.elapsed()).expect("pause duration");
    push_frames(&appsrc(&pipeline), 5..10, gst::ClockTime::ZERO);
    finish(&pipeline);

    let after = grain_head(&reader);
    let now = instance.get_current_index(&VIDEO_RATE);
    assert_consecutive_grains(&reader, after, 9, 4);
    pipeline.set_state(gst::State::Null).expect("Null");

    assert_near(
        "grains between the frames either side of the pause",
        (after - before) as i64,
        (5 + paused_for / FRAME) as i64,
        2,
    );
    assert_near("head minus MXL now", after as i64 - now as i64, 0, 2);
}

/// Pausing and resuming must not move the audio writer back over samples it
/// has already committed, and writing must resume at MXL now.
#[test]
fn pause_and_resume_audio_writes_at_mxl_now() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("pause_resume_audio");
    let domain = domain_guard.domain();
    let flow_id = uuid::Uuid::new_v4().to_string();
    let pipeline = launch(&format!(
        "appsrc name=src format=time caps={AUDIO_CAPS} \
         ! mxlsink name=sink flow-id={flow_id} domain={domain}"
    ));
    let sink = pipeline.by_name("sink").expect("mxlsink");
    let instance = mxl_instance(&domain);

    pipeline.set_state(gst::State::Playing).expect("Playing");
    push_audio(&appsrc(&pipeline), 0..20, gst::ClockTime::ZERO);
    wait_rendered(&sink, 20);
    let reader = samples_reader(&instance, &flow_id);
    let before = samples_head(&reader);

    // Running time stops for as long as the pipeline is paused, which is
    // longer than GAP when the sleep overruns.
    let paused = Instant::now();
    pipeline.set_state(gst::State::Paused).expect("Paused");
    thread::sleep(GAP.into());
    pipeline.set_state(gst::State::Playing).expect("Playing");
    let paused_for = gst::ClockTime::try_from(paused.elapsed()).expect("pause duration");
    push_audio(&appsrc(&pipeline), 20..40, gst::ClockTime::ZERO);
    finish(&pipeline);

    let after = samples_head(&reader);
    let now = instance.get_current_index(&AUDIO_RATE);
    pipeline.set_state(gst::State::Null).expect("Null");

    let gap_samples = paused_for.nseconds() * 48 / 1_000_000;
    assert_near(
        "samples between the buffers either side of the pause",
        (after - before) as i64,
        (20 * SAMPLES_PER_BUFFER + gap_samples) as i64,
        2 * SAMPLES_PER_BUFFER as i64,
    );
    assert_near(
        "head minus MXL now",
        after as i64 - now as i64,
        0,
        2 * SAMPLES_PER_BUFFER as i64,
    );
}

/// A source that stops delivering while the pipeline stays PLAYING, then
/// resumes with timestamps that account for the gap, must leave exactly that
/// gap in the flow and write the resumed frames at MXL now.
#[test]
fn stopped_video_source_resumes_at_mxl_now() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("gap_video");
    let domain = domain_guard.domain();
    let flow_id = uuid::Uuid::new_v4().to_string();
    let pipeline = launch(&format!(
        "appsrc name=src format=time caps={} \
         ! mxlsink name=sink flow-id={flow_id} domain={domain}",
        video_caps(25, 1)
    ));
    let sink = pipeline.by_name("sink").expect("mxlsink");
    let instance = mxl_instance(&domain);

    pipeline.set_state(gst::State::Playing).expect("Playing");
    push_frames(&appsrc(&pipeline), 0..5, gst::ClockTime::ZERO);
    wait_rendered(&sink, 5);
    let reader = grain_reader(&instance, &flow_id);
    let before = grain_head(&reader);

    push_frames(&appsrc(&pipeline), 5..10, GAP);
    finish(&pipeline);

    let after = grain_head(&reader);
    let now = instance.get_current_index(&VIDEO_RATE);
    assert_consecutive_grains(&reader, after, 9, 4);
    pipeline.set_state(gst::State::Null).expect("Null");

    assert_eq!(after - before, 5 + GAP / FRAME, "grains across the gap");
    assert_near("head minus MXL now", after as i64 - now as i64, 0, 2);
}

/// As [`stopped_video_source_resumes_at_mxl_now`], for an audio flow.
#[test]
fn stopped_audio_source_resumes_at_mxl_now() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("gap_audio");
    let domain = domain_guard.domain();
    let flow_id = uuid::Uuid::new_v4().to_string();
    let pipeline = launch(&format!(
        "appsrc name=src format=time caps={AUDIO_CAPS} \
         ! mxlsink name=sink flow-id={flow_id} domain={domain}"
    ));
    let sink = pipeline.by_name("sink").expect("mxlsink");
    let instance = mxl_instance(&domain);

    pipeline.set_state(gst::State::Playing).expect("Playing");
    push_audio(&appsrc(&pipeline), 0..20, gst::ClockTime::ZERO);
    wait_rendered(&sink, 20);
    let reader = samples_reader(&instance, &flow_id);
    let before = samples_head(&reader);

    push_audio(&appsrc(&pipeline), 20..40, GAP);
    finish(&pipeline);

    let after = samples_head(&reader);
    let now = instance.get_current_index(&AUDIO_RATE);
    pipeline.set_state(gst::State::Null).expect("Null");

    let gap_samples = GAP.nseconds() * 48 / 1_000_000;
    assert_eq!(
        after - before,
        20 * SAMPLES_PER_BUFFER + gap_samples,
        "samples across the gap"
    );
    assert_near(
        "head minus MXL now",
        after as i64 - now as i64,
        0,
        2 * SAMPLES_PER_BUFFER as i64,
    );
}

/// Audio whose segment starts at 10 s, as a demuxer sends when playing a file,
/// must be written at MXL now, not 10 s ahead.
#[test]
fn audio_segment_start_offset_writes_at_mxl_now() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("segment_start_audio");
    let domain = domain_guard.domain();
    let flow_id = uuid::Uuid::new_v4().to_string();
    let pipeline = launch(&format!(
        "appsrc name=src format=time handle-segment-change=true caps={AUDIO_CAPS} \
         ! mxlsink flow-id={flow_id} domain={domain}"
    ));
    let instance = mxl_instance(&domain);
    let start = gst::ClockTime::from_seconds(10);
    let mut segment = gst::FormattedSegment::<gst::ClockTime>::new();
    segment.set_start(start);
    segment.set_time(start);

    pipeline.set_state(gst::State::Playing).expect("Playing");
    let src = appsrc(&pipeline);
    for i in 0..20 {
        let sample = gst::Sample::builder()
            .buffer(&audio_buffer(start + AUDIO_BUFFER * i))
            .segment(&segment)
            .build();
        src.push_sample(&sample).expect("push sample");
    }
    finish(&pipeline);

    let head = samples_head(&samples_reader(&instance, &flow_id));
    let now = instance.get_current_index(&AUDIO_RATE);
    pipeline.set_state(gst::State::Null).expect("Null");

    assert_near(
        "head minus MXL now",
        head as i64 - now as i64,
        0,
        2 * SAMPLES_PER_BUFFER as i64,
    );
}

/// `mxlsrc ! mxlsink` must write each grain at the index it was read from.
#[test]
fn relay_keeps_grain_indices() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink", "mxlsrc"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("relay_indices");
    let domain = domain_guard.domain();
    let input = uuid::Uuid::new_v4().to_string();
    let output = uuid::Uuid::new_v4().to_string();
    let producer = launch(&format!(
        "appsrc name=src format=time caps={} \
         ! mxlsink name=sink flow-id={input} domain={domain}",
        video_caps(25, 1)
    ));
    let relay = launch(&format!(
        "mxlsrc video-flow-id={input} domain={domain} \
         ! mxlsink name=sink flow-id={output} domain={domain}"
    ));
    let instance = mxl_instance(&domain);

    producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    push_frames(&appsrc(&producer), 0..1, gst::ClockTime::ZERO);
    wait_rendered(&producer.by_name("sink").expect("mxlsink"), 1);
    relay.set_state(gst::State::Playing).expect("relay Playing");
    push_frames(&appsrc(&producer), 1..50, gst::ClockTime::ZERO);
    finish(&producer);

    let input_reader = grain_reader(&instance, &input);
    let input_head = grain_head(&input_reader);
    assert_eq!(marker_at(&input_reader, input_head), 49);
    let deadline = Instant::now() + TIMEOUT;
    let output_head = loop {
        if let Ok(reader) = instance.create_flow_reader(&output) {
            let reader = reader.to_grain_reader().expect("grain reader");
            let head = grain_head(&reader);
            if head != u64::MAX && marker_at(&reader, head) == 49 {
                break head;
            }
        }
        assert!(Instant::now() < deadline, "relay did not write frame 49");
        thread::sleep(Duration::from_millis(1));
    };
    assert_bus_no_errors("relay", &collect_bus_errors(&relay));
    relay.set_state(gst::State::Null).expect("relay Null");
    producer.set_state(gst::State::Null).expect("producer Null");

    assert_eq!(
        output_head, input_head,
        "relay wrote frame 49 at another index"
    );
}

/// `alignment-threshold` and `discont-wait` apply to audio flows only, so a
/// video sink with either one set posts a warning.
#[test]
fn audio_properties_on_video_sink_post_warning() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("audio_props_on_video");
    let domain = domain_guard.domain();
    let flow_id = uuid::Uuid::new_v4().to_string();
    let pipeline = launch(&format!(
        "appsrc name=src format=time caps={} \
         ! mxlsink name=sink discont-wait=1000000000 flow-id={flow_id} domain={domain}",
        video_caps(25, 1)
    ));

    pipeline.set_state(gst::State::Playing).expect("Playing");
    push_frames(&appsrc(&pipeline), 0..1, gst::ClockTime::ZERO);
    let message = pipeline.bus().expect("bus").timed_pop_filtered(
        gst::ClockTime::from_seconds(TIMEOUT.as_secs()),
        &[gst::MessageType::Warning, gst::MessageType::Error],
    );
    pipeline.set_state(gst::State::Null).expect("Null");

    let message = message.expect("no warning from the video sink");
    let gst::MessageView::Warning(warning) = message.view() else {
        panic!("expected a warning, got {message:?}");
    };
    assert!(
        warning.error().matches(gst::LibraryError::Settings),
        "unexpected warning: {}",
        warning.error()
    );
}

/// Replace a running `mxlsrc` the way `nmossrc` does: set it to NULL, remove
/// it, and add a new one while the pipeline stays PLAYING. The relay's
/// `mxlsink` must keep writing at MXL now without an error.
fn swap_running_source(
    test: &str,
    producer_desc: &str,
    src_flow_property: &str,
    rate: &Rational,
    head: fn(&MxlInstance, &str) -> u64,
    tolerance: i64,
) {
    let domain_guard = TestDomainGuard::new(test);
    let domain = domain_guard.domain();
    let input = uuid::Uuid::new_v4().to_string();
    let output = uuid::Uuid::new_v4().to_string();
    let producer = launch(&format!(
        "{producer_desc} ! mxlsink name=sink flow-id={input} domain={domain}"
    ));
    let relay = gst::Pipeline::new();
    let make_src = || {
        gst::ElementFactory::make("mxlsrc")
            .property(src_flow_property, &input)
            .property("domain", &domain)
            .build()
            .expect("mxlsrc")
    };
    let sink = gst::ElementFactory::make("mxlsink")
        .property("flow-id", &output)
        .property("domain", &domain)
        .build()
        .expect("mxlsink");
    let src = make_src();
    relay.add_many([&src, &sink]).expect("add relay");
    src.link(&sink).expect("link relay");
    let instance = mxl_instance(&domain);

    producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    wait_rendered(&producer.by_name("sink").expect("mxlsink"), 1);
    relay.set_state(gst::State::Playing).expect("relay Playing");
    wait_rendered(&sink, 10);

    src.set_state(gst::State::Null).expect("old mxlsrc Null");
    relay.remove(&src).expect("remove old mxlsrc");
    let src = make_src();
    relay.add(&src).expect("add new mxlsrc");
    src.link(&sink).expect("link new mxlsrc");
    src.sync_state_with_parent().expect("sync new mxlsrc");
    let swapped = rendered(&sink);
    wait_rendered(&sink, swapped + 10);

    let errors = collect_bus_errors(&relay);
    let head = head(&instance, &output);
    let now = instance.get_current_index(rate);
    relay.set_state(gst::State::Null).expect("relay Null");
    producer.set_state(gst::State::Null).expect("producer Null");

    assert_bus_no_errors("relay", &errors);
    assert_near(
        "relay head minus MXL now",
        head as i64 - now as i64,
        0,
        tolerance,
    );
}

#[test]
fn swapped_video_source_keeps_relay_writing() {
    init();
    if let Some(reason) = skip_reason(&["videotestsrc", "mxlsink", "mxlsrc"]) {
        skip!(reason);
    }
    swap_running_source(
        "swap_video",
        &format!("videotestsrc is-live=true ! {}", video_caps(25, 1)),
        "video-flow-id",
        &VIDEO_RATE,
        |instance, flow_id| grain_head(&grain_reader(instance, flow_id)),
        3,
    );
}

#[test]
fn swapped_audio_source_keeps_relay_writing() {
    init();
    if let Some(reason) = skip_reason(&["audiotestsrc", "mxlsink", "mxlsrc"]) {
        skip!(reason);
    }
    swap_running_source(
        "swap_audio",
        &format!("audiotestsrc is-live=true samplesperbuffer={SAMPLES_PER_BUFFER} ! {AUDIO_CAPS}"),
        "audio-flow-id",
        &AUDIO_RATE,
        |instance, flow_id| samples_head(&samples_reader(instance, flow_id)),
        3 * SAMPLES_PER_BUFFER as i64,
    );
}

/// Where the first frame lands relative to the MXL grain it rounds to.
#[derive(Clone, Copy, Debug)]
enum Phase {
    /// On the grain's own timestamp, half a grain from either rounding boundary.
    MidGrain,
    /// Half a grain later, on the boundary where rounding moves to the next grain.
    RoundingBoundary,
}

/// Frames at a fractional rate, with PTS rounded to 90 kHz ticks as an MPEG-TS
/// demuxer produces them, must map to consecutive grains: rounding must not put
/// two frames in one grain (which the writer rejects) or skip a grain.
///
/// The pipeline runs on a TAI system clock, the clock MXL time is read from, so
/// the sampled offset is zero, and `base_time` is set explicitly. That pins
/// where frame 0 lands in MXL time.
fn fractional_rate_frames_map_to_consecutive_grains(test: &str, phase: Phase) {
    const FRAMES: u64 = 30;
    let domain_guard = TestDomainGuard::new(test);
    let domain = domain_guard.domain();
    let instance = mxl_instance(&domain);
    let tai: gst::Clock = glib::Object::builder::<gst::SystemClock>()
        .property("clock-type", gst::ClockType::Tai)
        .build()
        .upcast();

    for (numerator, denominator) in [(24_000, 1_001), (30_000, 1_001), (60_000, 1_001)] {
        let rate = Rational {
            numerator: numerator as i64,
            denominator: denominator as i64,
        };
        let flow_id = uuid::Uuid::new_v4().to_string();
        let pipeline = launch(&format!(
            "appsrc name=src format=time caps={} \
             ! mxlsink flow-id={flow_id} domain={domain}",
            video_caps(numerator, denominator)
        ));
        pipeline.use_clock(Some(&tai));
        pipeline.set_start_time(gst::ClockTime::NONE);
        let first = instance.get_current_index(&rate) + 10;
        let half_grain = 500_000_000 * denominator as u64 / numerator as u64;
        let base_time = instance
            .index_to_timestamp(first, &rate)
            .expect("timestamp")
            + match phase {
                Phase::MidGrain => 0,
                Phase::RoundingBoundary => half_grain,
            };
        pipeline.set_base_time(gst::ClockTime::from_nseconds(base_time));

        let pts = |i: u64| {
            let ticks =
                (2 * i * 90_000 * denominator as u64 + numerator as u64) / (2 * numerator as u64);
            gst::ClockTime::from_nseconds(ticks * 1_000_000_000 / 90_000)
        };
        let bytes = frame_bytes(numerator, denominator);
        pipeline.set_state(gst::State::Playing).expect("Playing");
        let src = appsrc(&pipeline);
        for i in 0..FRAMES {
            src.push_buffer(video_frame(bytes, pts(i), pts(i + 1) - pts(i), i as u8))
                .expect("push frame");
        }
        finish(&pipeline);

        let reader = grain_reader(&instance, &flow_id);
        let head = grain_head(&reader);
        assert_consecutive_grains(&reader, head, (FRAMES - 1) as u8, 3);
        pipeline.set_state(gst::State::Null).expect("Null");
        if let Phase::MidGrain = phase {
            assert_eq!(
                head,
                first + FRAMES - 1,
                "{numerator}/{denominator}: last frame's grain"
            );
        }
    }
}

#[test]
fn fractional_rate_frames_mid_grain_map_to_consecutive_grains() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    fractional_rate_frames_map_to_consecutive_grains("fractional_mid", Phase::MidGrain);
}

#[test]
fn fractional_rate_frames_on_rounding_boundary_map_to_consecutive_grains() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    fractional_rate_frames_map_to_consecutive_grains(
        "fractional_boundary",
        Phase::RoundingBoundary,
    );
}
