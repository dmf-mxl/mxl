// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Integration tests for GStreamer video with ancillary metadata being written
//! to a video MXL flow and a data MXL flow and reading these flows back.
//!
//! Both tests are `#[ignore]` so `cargo test` skips them by default; run with
//! `cargo test -p gst-mxl-rs --test video_data_sync -- --ignored`.
//!
//! Both tests use the same producer pipeline. The `appsrc` writes a frame index
//! into the v210 payload and every ancillary payload.
//!
//! ```text
//! appsrc (v210 + GstAncillaryMeta)
//!   ! st2038extractor name=ext remove-ancillary-meta=true
//! ext.src     ! queue ! mxlsink (video flow)
//! ext.st2038  ! queue ! mxlsink (data flow)
//! ```
//!
//! 1. [`v210_with_meta_to_v210_and_st2038_via_mxl`]: reads the flows with a
//!    disjoint consumer pipeline with two `appsink`s, which record the timestamps
//!    of and the frame index read out of every sample, and compares video vs data
//!    for shared frame index.
//!
//!    ```text
//!    mxlsrc (video flow) ! queue ! appsink (v210)
//!    mxlsrc (data flow)  ! queue ! appsink (meta/x-st-2038)
//!    ```
//!
//! 2. [`v210_with_meta_to_v210_with_meta_via_mxl`]: reads the flows with a
//!    single-`appsink` consumer pipeline, joining both flows back together
//!    via `st2038combiner`, which re-attaches the ancillary metadata on the
//!    matching video buffers.
//!
//!    ```text
//!    mxlsrc (video flow) ! queue min-threshold-buffers=2 ! comb.sink
//!    mxlsrc (data flow)  ! queue                         ! comb.st2038
//!    st2038combiner name=comb ! queue
//!      ! appsink (v210 + GstAncillaryMeta)
//!    ```

use std::path::PathBuf;
use std::sync::Once;

use gst::prelude::*;
use gstmxl::format::data::ancillary_meta_from_st2038_anc_packet;
use gstreamer as gst;
use gstreamer_app as gst_app;
use gstreamer_video as gst_video;

const FRAMERATE_NUM: i32 = 30_000;
const FRAMERATE_DEN: i32 = 1_001;
const FRAME_PERIOD_NS: u64 =
    gst::ClockTime::SECOND.nseconds() * FRAMERATE_DEN as u64 / FRAMERATE_NUM as u64;
// v210 pads lines to a multiple of 128 bytes; width >= 2 is valid.
const VIDEO_WIDTH: u32 = 2;
const VIDEO_HEIGHT: u32 = 2;

static REGISTER: Once = Once::new();

fn init() {
    REGISTER.call_once(|| {
        gst::init().expect("gst::init");
        gst::Element::register(
            None,
            "mxlsrc",
            gst::Rank::NONE,
            gstmxl::mxlsrc::MxlSrc::static_type(),
        )
        .expect("register mxlsrc");
        gst::Element::register(
            None,
            "mxlsink",
            gst::Rank::NONE,
            gstmxl::mxlsink::MxlSink::static_type(),
        )
        .expect("register mxlsink");
    });
}

fn require_factories(names: &[&str]) {
    let missing: Vec<&str> = names
        .iter()
        .filter(|n| gst::ElementFactory::find(n).is_none())
        .copied()
        .collect();
    assert!(missing.is_empty(), "missing element factories: {missing:?}");
}

fn extend_with_even_odd_parity(v: u8) -> u16 {
    if v.count_ones() & 1 == 0 {
        0x1_00 | (v as u16)
    } else {
        0x2_00 | (v as u16)
    }
}

fn compute_checksum(did_10bit: u16, sdid_10bit: u16, dc_10bit: u16, data: &[u16]) -> u16 {
    let mut checksum = 0u16;
    checksum = checksum.wrapping_add(did_10bit & 0x1ff);
    checksum = checksum.wrapping_add(sdid_10bit & 0x1ff);
    checksum = checksum.wrapping_add(dc_10bit & 0x1ff);
    for &w in data {
        checksum = checksum.wrapping_add(w & 0x1ff);
    }
    checksum &= 0x1ff;
    checksum |= ((!(checksum >> 8)) & 0x01) << 9;
    checksum
}

fn add_ancillary_meta(
    buffer: &mut gst::BufferRef,
    line: u16,
    offset: u16,
    did: u8,
    sdid: u8,
    payload: &[u8],
) {
    let mut meta = gst_video::video_meta::AncillaryMeta::add(buffer);
    meta.set_c_not_y_channel(false);
    meta.set_line(line);
    meta.set_offset(offset);

    let did_10bit = extend_with_even_odd_parity(did);
    let sdid_10bit = extend_with_even_odd_parity(sdid);
    let dc_10bit = extend_with_even_odd_parity(payload.len() as u8);

    meta.set_did(did_10bit);
    meta.set_sdid_block_number(sdid_10bit);

    let data: Vec<u16> = payload
        .iter()
        .copied()
        .map(extend_with_even_odd_parity)
        .collect();
    meta.set_checksum(compute_checksum(did_10bit, sdid_10bit, dc_10bit, &data));
    meta.set_data(glib::Slice::from(data));
}

/// Per-test MXL domain under `/dev/shm`, removed on drop.
///
/// Mirrors `tests/data_round_trip.rs::TestDomainGuard`. Linux-only, like the
/// existing GStreamer in-process tests in this crate.
struct TestDomainGuard {
    dir: PathBuf,
}

impl TestDomainGuard {
    fn new(test: &str) -> Self {
        let dir = PathBuf::from(format!(
            "/dev/shm/mxl_gst_test_domain_{test}_{}",
            uuid::Uuid::new_v4()
        ));
        std::fs::create_dir_all(&dir)
            .unwrap_or_else(|e| panic!("create test domain {}: {e}", dir.display()));
        Self { dir }
    }

    fn domain(&self) -> String {
        self.dir.to_string_lossy().into_owned()
    }
}

impl Drop for TestDomainGuard {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.dir);
    }
}

/// `parse::launch` the shared producer (appsrc → extractor → two mxlsinks)
/// and return the pipeline, its appsrc, and the v210 frame size in bytes.
fn build_producer(
    video_flow_id: &str,
    data_flow_id: &str,
    domain: &str,
) -> (gst::Pipeline, gst_app::AppSrc, usize) {
    let video_info =
        gst_video::VideoInfo::builder(gst_video::VideoFormat::V210, VIDEO_WIDTH, VIDEO_HEIGHT)
            .fps(gst::Fraction::new(FRAMERATE_NUM, FRAMERATE_DEN))
            .build()
            .expect("v210 VideoInfo");
    let frame_bytes = video_info.size();

    // `ext.st2038` references the extractor's *sometimes* pad by name;
    // gst-launch defers that link until the pad appears so the whole
    // producer fits in one parse string with no `pad-added` glue.
    let producer_desc = format!(
        "appsrc name=src format=time \
           caps=video/x-raw,format=v210,\
                width={VIDEO_WIDTH},\
                height={VIDEO_HEIGHT},\
                framerate={FRAMERATE_NUM}/{FRAMERATE_DEN} \
           ! st2038extractor name=ext remove-ancillary-meta=true \
         ext.src \
           ! queue \
           ! mxlsink flow-id={video_flow_id} domain={domain} \
         ext.st2038 \
           ! queue \
           ! mxlsink flow-id={data_flow_id} domain={domain}"
    );
    let producer = gst::parse::launch(&producer_desc)
        .expect("parse producer")
        .downcast::<gst::Pipeline>()
        .expect("producer pipeline");
    let appsrc = producer
        .by_name("src")
        .expect("appsrc")
        .downcast::<gst_app::AppSrc>()
        .expect("AppSrc downcast");
    (producer, appsrc, frame_bytes)
}

/// Push `count` v210 buffers. Each buffer stamps its frame index into byte 0
/// of the v210 payload **and** into byte 0 of every ancillary payload, so the
/// combiner consumer can verify that the re-attached `GstAncillaryMeta`s came
/// from the *same* producer frame as the carrying video buffer.
fn push_test_frames(appsrc: &gst_app::AppSrc, frame_bytes: usize, count: usize) {
    for i in 0..count {
        // Explicit PTS at integer multiples of the frame period maps each
        // push deterministically to a distinct MXL grain index on both flows
        // (mxlsink paces commits against the MXL clock internally based on
        // PTS), so we don't sleep here.
        let pts = gst::ClockTime::from_nseconds(i as u64 * FRAME_PERIOD_NS);
        let mut buf = gst::Buffer::with_size(frame_bytes).expect("v210 buffer");
        let frame_idx = i as u8;
        {
            let b = buf.get_mut().expect("buffer mut");
            b.set_pts(pts);
            b.set_duration(gst::ClockTime::from_nseconds(FRAME_PERIOD_NS));
            {
                let mut map = b.map_writable().expect("buffer writable");
                map.as_mut_slice()[0] = frame_idx;
            }
            add_ancillary_meta(b, 9, 0, 0x44, 0x01, &[frame_idx, 0xa, 0xaa, 0x55]);
            add_ancillary_meta(b, 9, 32, 0x44, 0x02, &[frame_idx, 0xb, 0xaa, 0x55]);
        }
        appsrc.push_buffer(buf).expect("push v210 buffer");
    }
}

/// Recover the original byte from a 10-bit-with-even/odd-parity ANC word.
/// Inverse of `extend_with_even_odd_parity`.
fn ancillary_byte(word_10bit: u16) -> u8 {
    (word_10bit & 0xff) as u8
}

/// Last `GstAncillaryMeta` on a v210 buffer: producer frame stamp in user-data word 0 low byte.
fn last_ancillary_meta_data0(buffer: &gst::BufferRef) -> Option<u8> {
    let meta = buffer
        .iter_meta::<gst_video::video_meta::AncillaryMeta>()
        .last()?;
    Some(ancillary_byte(
        *meta.data().first().expect("ancillary meta had empty data"),
    ))
}

/// First ST 2038 ANC packet in a `meta/x-st-2038` buffer: producer stamp in byte 0 of user data.
fn st2038_first_packet_data0(st2038: &[u8]) -> u8 {
    let mut rem = st2038;
    let meta = ancillary_meta_from_st2038_anc_packet(&mut rem).expect("parse first ST 2038 ANC");
    ancillary_byte(*meta.data.first().expect("ST 2038 data_count was zero"))
}

/// PTS and running time from a `gst::Sample`.
fn timing_from_sample(sample: &gst::SampleRef) -> (Option<gst::ClockTime>, Option<gst::ClockTime>) {
    let Some(buffer) = sample.buffer() else {
        return (None, None);
    };
    let pts = buffer.pts();
    let running_time = match (pts, sample.segment()) {
        (Some(pts), Some(seg)) => seg
            .downcast_ref::<gst::ClockTime>()
            .and_then(|clock_seg| clock_seg.to_running_time(pts)),
        _ => None,
    };
    (pts, running_time)
}

/// PTS, running time, and frame index from one `appsink` sample.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct SampleTiming {
    pts: Option<gst::ClockTime>,
    running_time: Option<gst::ClockTime>,
    /// Frame index read out of v210 byte 0, or first ST 2038 ANC user byte 0.
    frame_idx: u8,
}

fn pull_all_samples(
    sink: &gst_app::AppSink,
    pull_timeout: gst::ClockTime,
    mut frame_from_buffer: impl FnMut(&gst::BufferRef) -> u8,
) -> Vec<SampleTiming> {
    let mut out = Vec::new();
    while let Some(sample) = sink.try_pull_sample(pull_timeout) {
        let buffer = sample.buffer().expect("sample buffer");
        let (pts, running_time) = timing_from_sample(&sample);
        out.push(SampleTiming {
            pts,
            running_time,
            frame_idx: frame_from_buffer(buffer),
        });
    }
    out
}

fn compare_disjoint_video_data(video: &[SampleTiming], data: &[SampleTiming], push_count: usize) {
    use std::collections::{BTreeMap, BTreeSet};

    let mut video_by_frame = BTreeMap::new();
    for s in video {
        assert!(
            video_by_frame.insert(s.frame_idx, *s).is_none(),
            "duplicate video sample for frame_idx {}",
            s.frame_idx,
        );
    }
    let mut data_by_frame = BTreeMap::new();
    for s in data {
        assert!(
            data_by_frame.insert(s.frame_idx, *s).is_none(),
            "duplicate data sample for frame_idx {}",
            s.frame_idx,
        );
    }

    let video_keys: BTreeSet<_> = video_by_frame.keys().copied().collect();
    let data_keys: BTreeSet<_> = data_by_frame.keys().copied().collect();
    let expected: BTreeSet<u8> = (0..push_count).map(|i| i as u8).collect();

    let only_video: Vec<_> = video_keys.difference(&data_keys).copied().collect();
    let only_data: Vec<_> = data_keys.difference(&video_keys).copied().collect();
    eprintln!(
        "disjoint flow: video {} sample(s), data {} sample(s); \
         only-video frame_idx {only_video:?}, only-data frame_idx {only_data:?}",
        video.len(),
        data.len(),
    );

    let missing_video: Vec<_> = expected.difference(&video_keys).copied().collect();
    let missing_data: Vec<_> = expected.difference(&data_keys).copied().collect();
    if !missing_video.is_empty() || !missing_data.is_empty() {
        eprintln!(
            "expected 0..{}: missing video frame_idx {missing_video:?}, missing data frame_idx {missing_data:?}",
            push_count,
        );
    }

    for &f in video_keys.intersection(&data_keys) {
        let v = video_by_frame[&f];
        let d = data_by_frame[&f];
        assert_eq!(
            v.pts, d.pts,
            "frame {f}: video pts {:?} vs data pts {:?} must match exactly",
            v.pts, d.pts,
        );
        assert_eq!(
            v.running_time, d.running_time,
            "frame {f}: video running_time {:?} vs data running_time {:?} must match exactly",
            v.running_time, d.running_time,
        );
    }
}

/// Reads the flows with a disjoint consumer pipeline with two `appsink`s.
/// Records PTS, running time, and producer frame index on every sample, then
/// compares video vs data for each frame index present on both sides.
#[test]
#[ignore = "MXL + GStreamer integration; opt-in with cargo test -- --ignored"]
fn v210_with_meta_to_v210_and_st2038_via_mxl() {
    init();
    require_factories(&[
        "appsrc",
        "appsink",
        "queue",
        "st2038extractor",
        "mxlsink",
        "mxlsrc",
    ]);

    let video_flow_id = uuid::Uuid::new_v4().to_string();
    let data_flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new("v210_with_meta");
    let domain = domain_guard.domain();

    let (producer, appsrc, frame_bytes) = build_producer(&video_flow_id, &data_flow_id, &domain);

    // Consumer: one pipeline with two disjoint chains, one `mxlsrc` per flow.
    // Both `mxlsrc`s wait for their flow to be created by the producer
    // (`FlowNotFound` poll loop is internal to `mxlsrc`), so the consumer can
    // be started before any buffers are pushed. `gst::parse::launch` accepts
    // multiple disjoint sub-pipelines in one description.
    let consumer_desc = format!(
        "mxlsrc video-flow-id={video_flow_id} domain={domain} \
           ! queue \
           ! appsink name=video_sink sync=false \
               caps=video/x-raw,format=v210 \
         mxlsrc data-flow-id={data_flow_id} domain={domain} \
           ! queue \
           ! appsink name=data_sink sync=false \
               caps=meta/x-st-2038,\
                    alignment=frame,\
                    framerate={FRAMERATE_NUM}/{FRAMERATE_DEN}"
    );
    let consumer = gst::parse::launch(&consumer_desc)
        .expect("parse consumer")
        .downcast::<gst::Pipeline>()
        .expect("consumer pipeline");
    let video_appsink = consumer
        .by_name("video_sink")
        .expect("video appsink")
        .downcast::<gst_app::AppSink>()
        .expect("video AppSink downcast");
    let data_appsink = consumer
        .by_name("data_sink")
        .expect("data appsink")
        .downcast::<gst_app::AppSink>()
        .expect("data AppSink downcast");

    // Producer first so both flows exist by the time the consumer `mxlsrc`s'
    // readers attach.
    producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    consumer
        .set_state(gst::State::Playing)
        .expect("consumer Playing");

    const PUSH_COUNT: usize = 150;
    push_test_frames(&appsrc, frame_bytes, PUSH_COUNT);

    let pull_timeout = gst::ClockTime::from_seconds(2);
    let video_samples = pull_all_samples(&video_appsink, pull_timeout, |buf| {
        assert_eq!(
            buf.size(),
            frame_bytes,
            "v210 round-trip should preserve frame size"
        );
        let map = buf.map_readable().expect("video buffer readable");
        map.as_slice()[0]
    });
    let data_samples = pull_all_samples(&data_appsink, pull_timeout, |buf| {
        assert!(
            buf.size() > 0,
            "ST 2038 round-trip buffer should be non-empty"
        );
        let map = buf.map_readable().expect("data buffer readable");
        st2038_first_packet_data0(map.as_slice())
    });

    compare_disjoint_video_data(&video_samples, &data_samples, PUSH_COUNT);

    producer.set_state(gst::State::Null).expect("producer Null");
    consumer.set_state(gst::State::Null).expect("consumer Null");
}

/// Reads the flows with a single-`appsink` consumer pipeline, joining both
/// flows back together via `st2038combiner`, which re-attaches the ancillary
/// metadata on the matching video buffers.
#[test]
#[ignore = "MXL + GStreamer integration; opt-in with cargo test -- --ignored"]
fn v210_with_meta_to_v210_with_meta_via_mxl() {
    init();
    require_factories(&[
        "appsrc",
        "appsink",
        "queue",
        "st2038extractor",
        "st2038combiner",
        "mxlsink",
        "mxlsrc",
    ]);

    let video_flow_id = uuid::Uuid::new_v4().to_string();
    let data_flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new("v210_with_meta");
    let domain = domain_guard.domain();

    let (producer, appsrc, frame_bytes) = build_producer(&video_flow_id, &data_flow_id, &domain);

    // The video queue holds back by one buffer (one frame), which is the
    // worst-case arrival skew the combiner needs to absorb: the data grain
    // for index X must have arrived before video grain X+1 is read by
    // `mxlsrc`, so one frame of slack is sufficient.
    let consumer_desc = format!(
        "mxlsrc video-flow-id={video_flow_id} domain={domain} \
           ! queue min-threshold-buffers=2 \
           ! comb.sink \
         mxlsrc data-flow-id={data_flow_id} domain={domain} \
           ! queue \
           ! comb.st2038 \
         st2038combiner name=comb \
           ! queue \
           ! appsink name=sink sync=false caps=video/x-raw,format=v210"
    );
    let consumer = gst::parse::launch(&consumer_desc)
        .expect("parse combiner consumer")
        .downcast::<gst::Pipeline>()
        .expect("combiner consumer pipeline");
    let appsink = consumer
        .by_name("sink")
        .expect("appsink")
        .downcast::<gst_app::AppSink>()
        .expect("AppSink downcast");

    producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    consumer
        .set_state(gst::State::Playing)
        .expect("consumer Playing");

    const PUSH_COUNT: usize = 150;
    push_test_frames(&appsrc, frame_bytes, PUSH_COUNT);

    let pull_timeout = gst::ClockTime::from_seconds(2);
    let mut pulled = 0usize;
    while let Some(sample) = appsink.try_pull_sample(pull_timeout) {
        let buffer = sample.buffer().expect("sample buffer");
        assert_eq!(
            buffer.size(),
            frame_bytes,
            "v210 round-trip should preserve frame size"
        );
        let Some(anc_frame_idx) = last_ancillary_meta_data0(buffer) else {
            // Combiner may output v210 before ancillary is re-attached for that frame.
            continue;
        };
        let video_frame_idx = {
            let map = buffer.map_readable().expect("video buffer readable");
            map.as_slice()[0]
        };
        assert_eq!(
            video_frame_idx, anc_frame_idx,
            "v210 byte 0 should match last ANC packet user-data byte 0",
        );
        pulled += 1;
    }
    assert!(
        pulled > 0,
        "expected at least one combined v210 buffer with GstAncillaryMeta from appsink"
    );

    producer.set_state(gst::State::Null).expect("producer Null");
    consumer.set_state(gst::State::Null).expect("consumer Null");
}
