// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Integration tests for where `mxlsink` places grains in time.
//!
//! Live sources start their segment at 0, where a buffer's PTS and running time
//! agree. Demuxers such as `tsdemux` start it at the media's first PTS. These
//! tests feed such a segment and check that grains are written by the running
//! time `GstBaseSink` syncs on, so they land at MXL now.

#[macro_use]
mod common;

use common::{TestDomainGuard, init, skip_reason};
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;
use gstreamer_video as gst_video;
use mxl::{MxlInstance, Rational, config::get_mxl_so_path};

/// 25 fps keeps the frame period a whole number of nanoseconds, so consecutive
/// frames map to consecutive grain indices without rounding slop.
const FPS: i32 = 25;
const FRAME_NS: u64 = 40_000_000;
const GRAIN_RATE: Rational = Rational {
    numerator: FPS as i64,
    denominator: 1,
};
const WIDTH: u32 = 2;
const HEIGHT: u32 = 2;
/// Far enough from 0 that a PTS-based mapping would land 250 grains ahead.
const SEGMENT_START: gst::ClockTime = gst::ClockTime::from_seconds(10);

struct Producer {
    pipeline: gst::Pipeline,
    appsrc: gst_app::AppSrc,
    frame_bytes: usize,
    segment: gst::FormattedSegment<gst::ClockTime>,
}

/// `appsrc` → video `mxlsink`, with every sample carrying a segment that
/// starts at [`SEGMENT_START`] (as `tsdemux` would send).
fn build_producer(flow_id: &str, domain: &str) -> Producer {
    let frame_bytes = gst_video::VideoInfo::builder(gst_video::VideoFormat::V210, WIDTH, HEIGHT)
        .fps(gst::Fraction::new(FPS, 1))
        .build()
        .expect("v210 VideoInfo")
        .size();
    let desc = format!(
        "appsrc name=src format=time handle-segment-change=true \
           caps=video/x-raw,format=v210,width={WIDTH},height={HEIGHT},framerate={FPS}/1 \
         ! mxlsink flow-id={flow_id} domain={domain}"
    );
    let pipeline = gst::parse::launch(&desc)
        .expect("parse producer")
        .downcast::<gst::Pipeline>()
        .expect("producer pipeline");
    let appsrc = pipeline
        .by_name("src")
        .expect("appsrc")
        .downcast::<gst_app::AppSrc>()
        .expect("AppSrc downcast");
    let mut segment = gst::FormattedSegment::<gst::ClockTime>::new();
    segment.set_start(SEGMENT_START);
    segment.set_time(SEGMENT_START);
    Producer {
        pipeline,
        appsrc,
        frame_bytes,
        segment,
    }
}

impl Producer {
    /// Push one frame whose first payload byte is `marker`.
    fn push(&self, pts: gst::ClockTime, marker: u8) {
        let mut buffer = gst::Buffer::with_size(self.frame_bytes).expect("v210 buffer");
        {
            let b = buffer.get_mut().expect("buffer mut");
            b.set_pts(pts);
            b.set_duration(gst::ClockTime::from_nseconds(FRAME_NS));
            b.map_writable().expect("buffer writable").as_mut_slice()[0] = marker;
        }
        let sample = gst::Sample::builder()
            .buffer(&buffer)
            .segment(&self.segment)
            .build();
        self.appsrc.push_sample(&sample).expect("push sample");
    }

    /// Signal EOS and wait for the sink to render everything (it syncs on the
    /// clock, so this returns once the last frame is due). Panics on a bus error.
    fn finish(&self) {
        self.appsrc.end_of_stream().expect("end of stream");
        let bus = self.pipeline.bus().expect("bus");
        let msg = bus
            .timed_pop_filtered(
                gst::ClockTime::from_seconds(5),
                &[gst::MessageType::Eos, gst::MessageType::Error],
            )
            .expect("timed out waiting for EOS");
        if let gst::MessageView::Error(err) = msg.view() {
            panic!(
                "producer error from {:?}: {} ({:?})",
                err.src().map(|s| s.path_string()),
                err.error(),
                err.debug()
            );
        }
    }
}

fn mxl_instance(domain: &str) -> MxlInstance {
    let api = mxl::load_api(get_mxl_so_path()).expect("load MXL API");
    MxlInstance::new(api, domain, "").expect("MXL instance")
}

/// Push ten frames from [`SEGMENT_START`], wait for them to render, and return
/// how many grains the flow's head index is ahead of MXL now.
fn head_minus_now_after_segment_start(test: &str) -> i64 {
    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new(test);
    let domain = domain_guard.domain();
    let producer = build_producer(&flow_id, &domain);

    producer
        .pipeline
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    for i in 0..10u64 {
        producer.push(
            SEGMENT_START + gst::ClockTime::from_nseconds(i * FRAME_NS),
            i as u8,
        );
    }
    producer.finish();

    let instance = mxl_instance(&domain);
    let reader = instance
        .create_flow_reader(&flow_id)
        .expect("flow reader")
        .to_grain_reader()
        .expect("grain reader");
    let head = reader.get_runtime_info().expect("runtime info").headIndex as i64;
    let now = instance.get_current_index(&GRAIN_RATE) as i64;

    producer
        .pipeline
        .set_state(gst::State::Null)
        .expect("producer Null");
    head - now
}

/// A segment starting at 10 s must not push grains 10 s into the future. The
/// head index right after the last frame renders has to be at MXL now.
#[test]
fn segment_start_offset_writes_grains_at_now() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let ahead = head_minus_now_after_segment_start("segment_start_offset");
    assert!(
        ahead.abs() <= 2,
        "head should be within 2 grains of now, was {ahead} ahead"
    );
}

/// A frame that starts half a frame before the segment start is still rendered
/// by `GstBaseSink`. It must be written (not dropped, no
/// error) in the grain just before the next frame's.
#[test]
fn writes_buffer_straddling_segment_start_in_sequence() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new("segment_straddle");
    let domain = domain_guard.domain();
    let producer = build_producer(&flow_id, &domain);

    producer
        .pipeline
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    // Frame k covers [start - FRAME/2 + k*FRAME, ...); frame 0 straddles the start.
    let first_pts = SEGMENT_START - gst::ClockTime::from_nseconds(FRAME_NS / 2);
    const FRAMES: u64 = 3;
    for k in 0..FRAMES {
        producer.push(
            first_pts + gst::ClockTime::from_nseconds(k * FRAME_NS),
            k as u8,
        );
    }
    producer.finish();

    let instance = mxl_instance(&domain);
    let reader = instance
        .create_flow_reader(&flow_id)
        .expect("flow reader")
        .to_grain_reader()
        .expect("grain reader");
    let head = reader.get_runtime_info().expect("runtime info").headIndex;
    for k in 0..FRAMES {
        let index = head - (FRAMES - 1 - k);
        let grain = reader
            .get_grain_non_blocking(index)
            .unwrap_or_else(|e| panic!("grain {index} (frame {k}) not readable: {e:?}"));
        assert_eq!(
            grain.index, index,
            "grain {index} was overwritten or missing"
        );
        assert_eq!(
            grain.payload[0], k as u8,
            "grain {index} should hold frame {k}"
        );
    }

    producer
        .pipeline
        .set_state(gst::State::Null)
        .expect("producer Null");
}
