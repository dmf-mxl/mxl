// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Integration tests for GStreamer video with ancillary metadata written to a
//! video MXL flow and a companion data MXL flow, then read back.
//!
//! Requires Linux `/dev/shm` (tmpfs) and the `gst-plugin-closedcaption`
//! elements (`st2038extractor`, `st2038combiner`). CI installs these from
//! [gst-plugins-rs](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs).
//!
//! The shared producer writes a frame index into the v210 payload and into
//! every ancillary payload:
//!
//! ```text
//! appsrc (v210 + GstAncillaryMeta)
//!   ! st2038extractor name=ext remove-ancillary-meta=true
//! ext.src     ! queue ! mxlsink (video flow)
//! ext.st2038  ! queue ! mxlsink (data flow)
//! ```
//!
//! `mxlsink` commits each frame at the absolute MXL grain index derived from
//! its PTS, so the same source frame lands at the *same* index on both flows.
//! `mxlsrc` derives PTS purely from that absolute index, so two independent
//! readers expose identical timestamps for the same frame — which is what these
//! tests assert, directly (disjoint `appsink`s) and via `st2038combiner`.
//!
//! Disjoint consumer (`v210_with_meta_to_v210_and_st2038_via_mxl`):
//!
//! ```text
//! mxlsrc (video flow) ! queue ! appsink video_sink (sync=false)
//! mxlsrc (data flow)  ! queue ! appsink data_sink  (sync=false)
//! ```
//!
//! Combiner consumer (`v210_with_meta_to_v210_with_meta_via_mxl`):
//!
//! ```text
//! mxlsrc (video flow) ! queue ! comb.sink
//! mxlsrc (data flow)  ! queue ! comb.st2038
//! st2038combiner name=comb drop-late-st2038=true ! queue ! appsink (sync=true)
//! ```

#[macro_use]
mod common;

use std::collections::{BTreeMap, BTreeSet};

use common::{
    FRAME_PERIOD_NS, FRAMERATE_DEN, FRAMERATE_NUM, add_ancillary_meta, assert_bus_no_errors,
    build_producer, collect_bus_errors, init, skip_reason, st2038_first_packet_data0,
};
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;
use gstreamer_video as gst_video;

/// Frames pushed per test — the stream length, not a ring figure (the ring holds
/// only a few grains). A reader that keeps pace stays within the ring's history
/// window and never sees eviction.
const PUSH_COUNT: usize = 150;
/// Maximum inter-flow attach offset, in frames. The video and data `mxlsrc`
/// readers each anchor at their flow's current head as it appears; they can
/// anchor up to this many frames apart, so only that many head frames may be
/// bare before both are attached. After attach there are no drops (in practice
/// the offset is at most one frame).
const ATTACH_SLACK: usize = 5;

/// Owns the producer and consumer pipelines and, on drop (including on panic),
/// stops the writers before the readers so no MXL worker thread outlives the
/// domain dir the `TestDomainGuard` removes (mirrors `data_round_trip`).
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

/// Push `count` v210 buffers. Each buffer stamps its frame index into byte 0
/// of the v210 payload **and** into byte 0 of every ancillary payload, so the
/// consumer can verify a sample's video and ancillary data came from the same
/// producer frame.
fn push_test_frames(appsrc: &gst_app::AppSrc, frame_bytes: usize, count: usize) {
    let frame_period = std::time::Duration::from_nanos(FRAME_PERIOD_NS);
    let start = std::time::Instant::now();
    for i in 0..count {
        // Hand each frame to mxlsink at its frame time, modelling a self-paced
        // real-time (live) source. mxlsink is a plain sync=true GstBaseSink: it
        // waits for each buffer's running time before committing, so it *holds* a
        // not-yet-due frame — but it cannot un-burst frames that are already past
        // due. If the producer burst everything at once and the run started late
        // (first render after base_time), the past-due early frames would commit
        // back-to-back and lap the small ring before a reader drains it (reader
        // jumps forward -> DISCONT -> gaps). Spacing production at frame cadence
        // makes frames arrive one-per-period, so commits stay paced regardless of
        // start latency.
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
        // Stop quietly if the pipeline is being torn down (push returns Flushing)
        // rather than panicking on a side thread.
        if appsrc.push_buffer(buf).is_err() {
            break;
        }
        // Sleep to this frame's slot on an absolute schedule (avoids the drift a
        // per-iteration sleep would accumulate); skip after the last frame.
        if i + 1 < count
            && let Some(remaining) = (start + frame_period * (i as u32 + 1))
                .checked_duration_since(std::time::Instant::now())
        {
            std::thread::sleep(remaining);
        }
    }
}

/// Bring both pipelines to `Playing` and stream the frames live.
///
/// The consumer can only reach `Playing` once its `appsink` prerolls, which
/// needs the producer to commit the first frame; but each `mxlsink` creates its
/// flow from the first buffer's caps, so nothing is committed until we push.
/// Gating the push on the consumer reaching `Playing` therefore deadlocks until
/// the `state()` timeout expires. Instead push on a side thread, concurrently
/// with waiting for the consumer to come up: both `mxlsrc` readers attach at the
/// producer's head as the flows appear, within `ATTACH_SLACK` frames.
fn start_and_stream(rt: &RoundTrip, appsrc: &gst_app::AppSrc, frame_bytes: usize) {
    rt.producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    rt.consumer
        .set_state(gst::State::Playing)
        .expect("consumer Playing");

    let push_src = appsrc.clone();
    let pusher = std::thread::spawn(move || push_test_frames(&push_src, frame_bytes, PUSH_COUNT));

    let (res, _, _) = rt.consumer.state(gst::ClockTime::from_seconds(10));
    res.expect("consumer reached Playing");
    assert_bus_no_errors("consumer", &collect_bus_errors(&rt.consumer));

    pusher.join().expect("push thread");
}

/// Recover the original byte from a 10-bit-with-even/odd-parity ANC word.
/// Inverse of `extend_with_even_odd_parity`.
fn ancillary_byte(word_10bit: u16) -> u8 {
    (word_10bit & 0xff) as u8
}

/// Distinct producer frame stamps across all `GstAncillaryMeta` on a v210 buffer.
/// The producer attaches two ancillary metas per frame, both stamped with the
/// same frame index, so a correctly paired frame yields a single stamp; a frame
/// that also swept up a late neighbour's ancillary yields more than one.
fn ancillary_meta_frame_stamps(buffer: &gst::BufferRef) -> BTreeSet<u8> {
    buffer
        .iter_meta::<gst_video::video_meta::AncillaryMeta>()
        .map(|meta| ancillary_byte(*meta.data().first().expect("ancillary meta had empty data")))
        .collect()
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

/// Collect samples from a live `appsink` until the final frame
/// (`PUSH_COUNT - 1`) arrives, the stream ends, or an overall deadline passes.
///
/// The readers are live, so pulling races the real-time stream and a per-pull
/// timeout alone cannot tell "no frame yet" from end-of-stream. The stream is
/// bounded and the last frame index is known, so stop once it is seen; also stop
/// promptly on EOS (`try_pull_sample` returns `None` immediately once the sink is
/// EOS, so poll `is_eos` rather than spinning), with the deadline as a backstop.
fn pull_all_samples(
    sink: &gst_app::AppSink,
    pull_timeout: gst::ClockTime,
    mut frame_from_buffer: impl FnMut(&gst::BufferRef) -> u8,
) -> Vec<SampleTiming> {
    let last_frame = (PUSH_COUNT - 1) as u8;
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
    let mut out = Vec::new();
    let mut seen_last = false;
    while !seen_last && std::time::Instant::now() < deadline {
        let Some(sample) = sink.try_pull_sample(pull_timeout) else {
            if sink.is_eos() {
                break;
            }
            continue;
        };
        let buffer = sample.buffer().expect("sample buffer");
        let (pts, running_time) = timing_from_sample(&sample);
        let frame_idx = frame_from_buffer(buffer);
        seen_last = frame_idx == last_frame;
        out.push(SampleTiming {
            pts,
            running_time,
            frame_idx,
        });
    }
    out
}

/// Index samples by frame, asserting no frame arrives twice.
fn by_frame(label: &str, samples: &[SampleTiming]) -> BTreeMap<u8, SampleTiming> {
    let mut map = BTreeMap::new();
    for s in samples {
        assert!(
            map.insert(s.frame_idx, *s).is_none(),
            "duplicate {label} sample for frame_idx {}",
            s.frame_idx,
        );
    }
    map
}

/// A live reader misses at most the first few frames (attach latency) and then
/// reads contiguously to the last frame pushed.
fn assert_contiguous_live_capture(label: &str, frames: &BTreeSet<u8>) {
    assert!(
        !frames.is_empty(),
        "expected {label} samples from mxlsrc, got 0 (pushed {PUSH_COUNT} frames)"
    );
    let first = *frames.iter().next().unwrap() as usize;
    let last = *frames.iter().next_back().unwrap() as usize;
    assert_eq!(
        last,
        PUSH_COUNT - 1,
        "{label} should read through the last frame {}, got {last}",
        PUSH_COUNT - 1
    );
    assert!(
        first <= ATTACH_SLACK,
        "{label} missed too many frames at attach: first read frame_idx {first}"
    );
    assert_eq!(
        frames.len(),
        last - first + 1,
        "{label} capture has gaps: {} frames over range {first}..={last}",
        frames.len(),
    );
}

fn compare_disjoint_video_data(video: &[SampleTiming], data: &[SampleTiming]) {
    let video_by_frame = by_frame("video", video);
    let data_by_frame = by_frame("data", data);
    let video_keys: BTreeSet<u8> = video_by_frame.keys().copied().collect();
    let data_keys: BTreeSet<u8> = data_by_frame.keys().copied().collect();

    eprintln!(
        "disjoint flow: video {} sample(s), data {} sample(s)",
        video.len(),
        data.len(),
    );
    assert_contiguous_live_capture("video", &video_keys);
    assert_contiguous_live_capture("data", &data_keys);

    // Both readers read contiguously to the last frame (checked above), each
    // attaching within ATTACH_SLACK of the head, so their overlap is short by at
    // most the inter-flow attach offset.
    let shared: Vec<u8> = video_keys.intersection(&data_keys).copied().collect();
    assert!(
        shared.len() >= PUSH_COUNT - ATTACH_SLACK,
        "video and data barely overlap ({} shared frames)",
        shared.len()
    );
    // The core invariant: the same source frame carries identical timing on both
    // flows, because both readers derive PTS from the same absolute grain index.
    for f in shared {
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

/// Reads the flows with a disjoint consumer pipeline with two `appsink`s and
/// compares video vs data timing for every frame present on both sides.
#[test]
fn v210_with_meta_to_v210_and_st2038_via_mxl() {
    init();
    const FACTORIES: &[&str] = &[
        "appsink",
        "appsrc",
        "mxlsink",
        "mxlsrc",
        "queue",
        "st2038extractor",
    ];
    if let Some(reason) = skip_reason(FACTORIES) {
        skip!(reason);
    }

    let video_flow_id = uuid::Uuid::new_v4().to_string();
    let data_flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = common::TestDomainGuard::new("v210_disjoint");
    let domain = domain_guard.domain();

    let (producer, appsrc, frame_bytes) = build_producer(&video_flow_id, &data_flow_id, &domain);

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

    let rt = RoundTrip { producer, consumer };
    start_and_stream(&rt, &appsrc, frame_bytes);

    let pull_timeout = gst::ClockTime::from_seconds(2);
    let video_samples = pull_all_samples(&video_appsink, pull_timeout, |buf| {
        assert_eq!(
            buf.size(),
            frame_bytes,
            "v210 round-trip should preserve frame size"
        );
        buf.map_readable()
            .expect("video buffer readable")
            .as_slice()[0]
    });
    let data_samples = pull_all_samples(&data_appsink, pull_timeout, |buf| {
        assert!(
            buf.size() > 0,
            "ST 2038 round-trip buffer should be non-empty"
        );
        st2038_first_packet_data0(buf.map_readable().expect("data buffer readable").as_slice())
    });

    compare_disjoint_video_data(&video_samples, &data_samples);
    drop(rt);
}

/// Reads the flows with a single-`appsink` consumer that joins both flows via
/// `st2038combiner`, which re-attaches the ancillary metadata onto the matching
/// video buffers. The combiner can only pair them because both flows expose the
/// same PTS per frame.
#[test]
fn v210_with_meta_to_v210_with_meta_via_mxl() {
    init();
    const FACTORIES: &[&str] = &[
        "appsink",
        "appsrc",
        "mxlsink",
        "mxlsrc",
        "queue",
        "st2038combiner",
        "st2038extractor",
    ];
    if let Some(reason) = skip_reason(FACTORIES) {
        skip!(reason);
    }
    // The `wrong`/`smear` assertions below only hold when the combiner drops
    // ancillary that misses its video frame's window. Builds without the
    // `drop-late-st2038` property instead collect late ancillary onto the next
    // picture, so skip rather than assert against behaviour the element lacks.
    if gst::ElementFactory::make("st2038combiner")
        .build()
        .ok()
        .and_then(|comb| comb.find_property("drop-late-st2038"))
        .is_none()
    {
        skip!("st2038combiner has no drop-late-st2038 property");
    }

    let video_flow_id = uuid::Uuid::new_v4().to_string();
    let data_flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = common::TestDomainGuard::new("v210_combiner");
    let domain = domain_guard.domain();

    let (producer, appsrc, frame_bytes) = build_producer(&video_flow_id, &data_flow_id, &domain);

    let consumer_desc = format!(
        "mxlsrc video-flow-id={video_flow_id} domain={domain} \
           ! queue \
           ! comb.sink \
         mxlsrc data-flow-id={data_flow_id} domain={domain} \
           ! queue \
           ! comb.st2038 \
         st2038combiner name=comb drop-late-st2038=true \
           ! queue \
           ! appsink name=sink sync=true caps=video/x-raw,format=v210"
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

    let rt = RoundTrip { producer, consumer };
    start_and_stream(&rt, &appsrc, frame_bytes);

    let pull_timeout = gst::ClockTime::from_seconds(2);
    // With `drop-late-st2038=true` the combiner drops ancillary that misses its
    // video frame's window instead of smearing it onto a later frame, so each
    // video frame N carries exactly its own ancillary N or nothing. `bare` is
    // then a clean count of frames whose ancillary did not arrive in time;
    // `wrong` (frame carries someone else's ancillary) must stay empty.
    let mut total = 0usize;
    let mut correct = 0usize;
    // A bare frame (drop-late dropped its ancillary) is only acceptable during
    // the initial attach, before both readers have anchored on the flows. Once
    // the first frame pairs its ancillary, both readers are attached and every
    // subsequent frame must carry its own ancillary — a bare frame after that is
    // a steady-state drop, which we do not tolerate.
    let mut attached = false;
    let mut first_idx: Option<u8> = None;
    let mut first_paired_idx: Option<u8> = None;
    let mut initial_bare: Vec<u8> = Vec::new();
    let mut steady_bare: Vec<u8> = Vec::new();
    let mut wrong: Vec<(usize, u8, Vec<u8>)> = Vec::new();
    let mut smear: Vec<(u8, Vec<u8>)> = Vec::new();
    // Pull until the final frame arrives, the sink signals EOS, or the deadline
    // passes (see `pull_all_samples`): a live pull races the real-time stream, so
    // a pull timeout alone cannot be read as end-of-stream.
    let last_frame = (PUSH_COUNT - 1) as u8;
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
    let mut seen_last = false;
    while !seen_last && std::time::Instant::now() < deadline {
        let Some(sample) = appsink.try_pull_sample(pull_timeout) else {
            if appsink.is_eos() {
                break;
            }
            continue;
        };
        let buffer = sample.buffer().expect("sample buffer");
        assert_eq!(
            buffer.size(),
            frame_bytes,
            "v210 round-trip should preserve frame size"
        );
        let pos = total;
        total += 1;
        let video_frame_idx = buffer
            .map_readable()
            .expect("video buffer readable")
            .as_slice()[0];
        first_idx.get_or_insert(video_frame_idx);
        seen_last = video_frame_idx == last_frame;
        let stamps = ancillary_meta_frame_stamps(buffer);
        if stamps.len() > 1 {
            smear.push((video_frame_idx, stamps.iter().copied().collect()));
        }
        if stamps.contains(&video_frame_idx) {
            correct += 1;
            attached = true;
            first_paired_idx.get_or_insert(video_frame_idx);
        } else if stamps.is_empty() {
            if attached {
                steady_bare.push(video_frame_idx);
            } else {
                initial_bare.push(video_frame_idx);
            }
        } else {
            wrong.push((pos, video_frame_idx, stamps.iter().copied().collect()));
        }
    }
    let paired = correct;
    // One machine-greppable line per run for the soak harness to classify drops.
    eprintln!(
        "COMBINER_SUMMARY total={total} paired={paired} first_idx={first_idx:?} \
         first_paired={first_paired_idx:?} initial_bare={} steady_bare={:?} wrong={} smear={}",
        initial_bare.len(),
        steady_bare,
        wrong.len(),
        smear.len(),
    );
    assert_bus_no_errors("consumer", &collect_bus_errors(&rt.consumer));
    // Every emitted frame carries its own ancillary or none: with the absolute
    // grain-index PTS the combiner must never attach another frame's ancillary
    // (`wrong`) or two frames' worth (`smear`).
    assert!(
        wrong.is_empty(),
        "combiner attached mismatched ancillary: {wrong:?}"
    );
    assert!(
        smear.is_empty(),
        "combiner attached multiple frames' ancillary: {smear:?}"
    );
    // No drops once both readers are attached.
    assert!(
        steady_bare.is_empty(),
        "combiner dropped ancillary in steady state (frames {steady_bare:?}); \
         initial_bare={initial_bare:?} first_paired={first_paired_idx:?}"
    );
    // Initial attach misses are bounded by the inter-flow attach offset: the two
    // readers anchor on the same flow head up to ATTACH_SLACK frames apart, so
    // only the first few head frames may be bare (in practice at most one).
    assert!(
        initial_bare.len() <= ATTACH_SLACK,
        "too many initial attach misses: {initial_bare:?}"
    );
    assert!(
        total >= PUSH_COUNT - ATTACH_SLACK,
        "combiner output too few frames: {total}"
    );
    assert!(
        paired >= PUSH_COUNT - ATTACH_SLACK,
        "combiner paired ancillary on too few frames: {paired}/{total}"
    );
    drop(rt);
}
