// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Integration test for **correct** `mxlsrc` timing behaviour.
//!
//! Asserts the reader contract across a consumer stall: whenever the delivered
//! PTS steps exactly one frame period, the grain content must advance exactly
//! one frame (no repeats or skips). This exercises the absolute-index read path
//! and the eviction catch-up. (Cross-flow PTS agreement within one pipeline is
//! covered by the disjoint test in `video_data_sync`.)

use std::thread;
use std::time::Duration;

#[macro_use]
mod common;

use common::{FRAME_PERIOD_NS, add_ancillary_meta, build_producer, init, skip_reason};
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;

fn make_v210_frame(frame_bytes: usize, frame_idx: u8, pts: gst::ClockTime) -> gst::Buffer {
    let mut buf = gst::Buffer::with_size(frame_bytes).expect("v210 buffer");
    {
        let b = buf.get_mut().expect("buffer mut");
        b.set_pts(pts);
        b.set_duration(gst::ClockTime::from_nseconds(FRAME_PERIOD_NS));
        {
            let mut map = b.map_writable().expect("buffer writable");
            map.as_mut_slice()[0] = frame_idx;
        }
        add_ancillary_meta(b, 9, 0, 0x44, 0x01, &[frame_idx, 0xa, 0xaa, 0x55]);
    }
    buf
}

fn push_frames(appsrc: &gst_app::AppSrc, frame_bytes: usize, start: usize, count: usize) {
    for i in start..start + count {
        let pts = gst::ClockTime::from_nseconds(i as u64 * FRAME_PERIOD_NS);
        let buf = make_v210_frame(frame_bytes, i as u8, pts);
        appsrc.push_buffer(buf).expect("push v210 buffer");
    }
}

#[derive(Debug, Clone, Copy)]
struct VideoSample {
    pts: gst::ClockTime,
    frame_idx: u8,
}

fn pull_video_samples(
    sink: &gst_app::AppSink,
    timeout: gst::ClockTime,
    frame_bytes: usize,
) -> Vec<VideoSample> {
    let mut out = Vec::new();
    while let Some(sample) = sink.try_pull_sample(timeout) {
        let buffer = sample.buffer().expect("sample buffer");
        let pts = buffer.pts().expect("PTS");
        let map = buffer.map_readable().expect("readable");
        assert_eq!(map.len(), frame_bytes);
        out.push(VideoSample {
            pts,
            frame_idx: map.as_slice()[0],
        });
    }
    out
}

/// Correct: PTS only ever advances by whole grain periods, and whenever it does
/// the content index must advance by the same number of frames. This includes
/// the multi-frame jump when the reader catches up after the stall — the very
/// transition the stall sets up — not just single-frame steps once it is back in
/// lockstep. (All frame indices here stay below 256, so the `u8` add is
/// unambiguous.)
fn assert_pts_tracks_content(samples: &[VideoSample]) {
    for w in samples.windows(2) {
        let prev = w[0];
        let next = w[1];
        let pts_delta = next.pts.nseconds().saturating_sub(prev.pts.nseconds());
        if pts_delta == 0 || pts_delta % FRAME_PERIOD_NS != 0 {
            continue;
        }
        let steps = (pts_delta / FRAME_PERIOD_NS) as u8;
        assert_eq!(
            next.frame_idx,
            prev.frame_idx.wrapping_add(steps),
            "PTS stepped {steps} frame(s) ({:?} -> {:?}) so content must advance {steps} \
             ({} -> {}), not repeat or skip",
            prev.pts,
            next.pts,
            prev.frame_idx,
            next.frame_idx
        );
    }
}

/// After a consumer stall, consecutive buffers must keep PTS aligned with grain content.
#[test]
fn lag_catch_up_pts_tracks_grain_content() {
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
    let domain_guard = common::TestDomainGuard::new("lag_catch_up");
    let domain = domain_guard.domain();

    let (producer, appsrc, frame_bytes) = build_producer(&video_flow_id, &data_flow_id, &domain);

    let consumer_desc = format!(
        "mxlsrc video-flow-id={video_flow_id} domain={domain} \
           ! queue \
           ! appsink name=video_sink sync=false \
               caps=video/x-raw,format=v210"
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

    producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    consumer
        .set_state(gst::State::Playing)
        .expect("consumer Playing");

    push_frames(&appsrc, frame_bytes, 0, 4);
    thread::sleep(Duration::from_millis(200));
    let _ = pull_video_samples(
        &video_appsink,
        gst::ClockTime::from_mseconds(500),
        frame_bytes,
    );

    consumer
        .set_state(gst::State::Paused)
        .expect("consumer Paused");
    push_frames(&appsrc, frame_bytes, 4, 40);
    thread::sleep(Duration::from_millis(100));
    consumer
        .set_state(gst::State::Playing)
        .expect("consumer Playing");

    let samples = pull_video_samples(&video_appsink, gst::ClockTime::from_seconds(3), frame_bytes);

    assert!(
        samples.len() >= 2,
        "need at least two samples after stall to verify PTS/content lockstep"
    );
    assert_pts_tracks_content(&samples);

    producer.set_state(gst::State::Null).expect("producer Null");
    consumer.set_state(gst::State::Null).expect("consumer Null");
}
