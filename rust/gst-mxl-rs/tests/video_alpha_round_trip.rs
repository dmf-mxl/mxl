// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! GStreamer integration test for `video/v210a` flows.
//!
//! `A422_10LE` frames go through `mxlsink` into a `video/v210a` flow in a per-test
//! MXL domain under `/dev/shm`, and are read back through `mxlsrc`. Every fill sample
//! must come back unchanged, and every alpha sample as it survives the flow's
//! video-range key (`gst_alpha_from_mxl_key(mxl_key_from_gst_alpha(a))`).
//!
//! ```text
//! appsrc (A422_10LE) ! mxlsink          (video/v210a flow)
//! mxlsrc ! appsink                      (A422_10LE)
//! ```
//!
//! The width is not a multiple of 6 or of 3, so the last v210 group and the last
//! key word of each line are partial.

#[macro_use]
mod common;

use std::collections::BTreeSet;

use common::{FRAME_PERIOD_NS, FRAMERATE_DEN, FRAMERATE_NUM, init, skip_reason};
use gst::prelude::*;
use gstmxl::format::video::{gst_alpha_from_mxl_key, mxl_key_from_gst_alpha};
use gstreamer as gst;
use gstreamer_app as gst_app;
use gstreamer_video::{self as gst_video, VideoFrameExt};

const WIDTH: u32 = 1280;
const HEIGHT: u32 = 8;
const PUSH_COUNT: usize = 30;

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

fn frame_info() -> gst_video::VideoInfo {
    gst_video::VideoInfo::builder(gst_video::VideoFormat::A42210le, WIDTH, HEIGHT)
        .fps(gst::Fraction::new(FRAMERATE_NUM, FRAMERATE_DEN))
        .build()
        .expect("A422_10LE VideoInfo")
}

/// Samples per line in each plane: Y, U, V, A.
fn plane_widths() -> [usize; 4] {
    let w = WIDTH as usize;
    [w, w.div_ceil(2), w.div_ceil(2), w]
}

/// The 10-bit samples of frame `index`, plane by plane, line by line. Seeded by
/// the index so the reader can rebuild what the writer sent. Sample 0 of the Y
/// plane carries the index itself, so the reader knows which frame it has.
fn expected_samples(index: usize) -> [Vec<u16>; 4] {
    let mut state = (index as u32).wrapping_mul(2_654_435_761) | 1;
    let mut next = || {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        (state & 0x3ff) as u16
    };
    let mut planes = plane_widths().map(|w| (0..w * HEIGHT as usize).map(|_| next()).collect());
    let y: &mut Vec<u16> = &mut planes[0];
    y[0] = index as u16;
    planes
}

fn write_frame(buffer: &mut gst::BufferRef, info: &gst_video::VideoInfo, index: usize) {
    let samples = expected_samples(index);
    let mut frame =
        gst_video::VideoFrameRef::from_buffer_ref_writable(buffer, info).expect("map frame");
    let strides: Vec<usize> = frame.plane_stride().iter().map(|&s| s as usize).collect();
    let planes = frame.planes_data_mut();
    for (p, data) in planes.into_iter().enumerate() {
        let width = plane_widths()[p];
        for line in 0..HEIGHT as usize {
            let row = &mut data[line * strides[p]..][..width * 2];
            for (x, chunk) in row.chunks_exact_mut(2).enumerate() {
                chunk.copy_from_slice(&samples[p][line * width + x].to_le_bytes());
            }
        }
    }
}

/// The frame's samples, plane by plane, without the stride padding.
fn read_frame(buffer: &gst::BufferRef, info: &gst_video::VideoInfo) -> [Vec<u16>; 4] {
    let frame =
        gst_video::VideoFrameRef::from_buffer_ref_readable(buffer, info).expect("map frame");
    std::array::from_fn(|p| {
        let width = plane_widths()[p];
        let stride = frame.plane_stride()[p] as usize;
        let data = frame.plane_data(p as u32).expect("plane data");
        let mut out = Vec::with_capacity(width * HEIGHT as usize);
        for line in 0..HEIGHT as usize {
            let row = &data[line * stride..][..width * 2];
            out.extend(
                row.chunks_exact(2)
                    .map(|b| u16::from_le_bytes([b[0], b[1]])),
            );
        }
        out
    })
}

/// Push the frames at frame cadence, as `video_data_sync` does, so commits stay
/// paced and the reader is not lapped.
fn push_frames(appsrc: &gst_app::AppSrc) {
    let info = frame_info();
    let period = std::time::Duration::from_nanos(FRAME_PERIOD_NS);
    let start = std::time::Instant::now();
    for i in 0..PUSH_COUNT {
        let mut buffer = gst::Buffer::with_size(info.size()).expect("buffer");
        {
            let b = buffer.get_mut().expect("buffer mut");
            b.set_pts(gst::ClockTime::from_nseconds(i as u64 * FRAME_PERIOD_NS));
            b.set_duration(gst::ClockTime::from_nseconds(FRAME_PERIOD_NS));
            write_frame(b, &info, i);
        }
        if appsrc.push_buffer(buffer).is_err() {
            break;
        }
        if i + 1 < PUSH_COUNT
            && let Some(remaining) =
                (start + period * (i as u32 + 1)).checked_duration_since(std::time::Instant::now())
        {
            std::thread::sleep(remaining);
        }
    }
}

/// A422_10LE → `mxlsink` → `video/v210a` → `mxlsrc` → A422_10LE, bit for bit.
#[test]
fn a422_10le_round_trip_via_v210a_flow() {
    init();
    const FACTORIES: &[&str] = &["appsink", "appsrc", "mxlsink", "mxlsrc", "queue"];
    if let Some(reason) = skip_reason(FACTORIES) {
        skip!(reason);
    }

    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = common::TestDomainGuard::new("video_alpha_round_trip");
    let domain = domain_guard.domain();

    let producer_desc = format!(
        "appsrc name=src format=time \
           caps=video/x-raw,format=A422_10LE,width={WIDTH},height={HEIGHT},\
                framerate={FRAMERATE_NUM}/{FRAMERATE_DEN} \
           ! queue \
           ! mxlsink flow-id={flow_id} domain={domain}"
    );
    let consumer_desc = format!(
        "mxlsrc video-flow-id={flow_id} domain={domain} \
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
    let appsrc = producer
        .by_name("src")
        .expect("appsrc")
        .downcast::<gst_app::AppSrc>()
        .expect("AppSrc downcast");
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
    // The flow only exists once mxlsink has the first buffer's caps, so push on a
    // side thread while the reader attaches.
    let pusher = std::thread::spawn(move || push_frames(&appsrc));

    let mut seen = BTreeSet::new();
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(20);
    while !seen.contains(&(PUSH_COUNT - 1)) && std::time::Instant::now() < deadline {
        let Some(sample) = appsink.try_pull_sample(gst::ClockTime::from_seconds(2)) else {
            if appsink.is_eos() {
                break;
            }
            continue;
        };
        let caps = sample.caps().expect("sample caps");
        let info = gst_video::VideoInfo::from_caps(caps).expect("video caps");
        assert_eq!(info.format(), gst_video::VideoFormat::A42210le);
        assert_eq!((info.width(), info.height()), (WIDTH, HEIGHT));

        let got = read_frame(sample.buffer().expect("sample buffer"), &info);
        let index = got[0][0] as usize;
        assert!(index < PUSH_COUNT, "frame index {index} out of range");
        let mut want = expected_samples(index);
        for alpha in want[3].iter_mut() {
            *alpha = gst_alpha_from_mxl_key(mxl_key_from_gst_alpha(*alpha));
        }
        for (p, name) in ["Y", "U", "V", "A"].iter().enumerate() {
            assert!(got[p] == want[p], "frame {index}: {name} plane differs");
        }
        assert!(seen.insert(index), "frame {index} delivered twice");
    }
    pusher.join().expect("push thread");

    // The flow definition lists the key plane as component "A".
    let flow_def_path = format!("{domain}/{flow_id}.mxl-flow/flow_def.json");
    let flow_def: serde_json::Value = serde_json::from_str(
        &std::fs::read_to_string(&flow_def_path).expect("read the flow definition"),
    )
    .expect("parse the flow definition");
    assert_eq!(flow_def["media_type"], "video/v210a");
    let names: Vec<&str> = flow_def["components"]
        .as_array()
        .expect("components")
        .iter()
        .filter_map(|c| c["name"].as_str())
        .collect();
    assert_eq!(names, ["Y", "Cb", "Cr", "A"]);

    for (label, pipeline) in [("consumer", &rt.consumer), ("producer", &rt.producer)] {
        common::assert_bus_no_errors(label, &common::collect_bus_errors(pipeline));
    }
    // A live reader may miss the first frames while it attaches, then reads every
    // frame to the end.
    let first = *seen.first().expect("no frames read back from mxlsrc");
    assert_eq!(
        seen.iter().copied().collect::<Vec<_>>(),
        (first..PUSH_COUNT).collect::<Vec<_>>(),
        "frames after attach must be contiguous"
    );
    assert!(first <= 5, "reader attached late, at frame {first}");
}

/// Interlaced caps are refused when they are set, for `v210` and `A422_10LE`
/// alike: MXL flows are progressive only for now. The producer must stop with an
/// error rather than write a flow labelled progressive.
#[test]
fn interlaced_caps_are_refused_by_mxlsink() {
    init();
    const FACTORIES: &[&str] = &["appsrc", "mxlsink"];
    if let Some(reason) = skip_reason(FACTORIES) {
        skip!(reason);
    }

    for format in [
        gst_video::VideoFormat::A42210le,
        gst_video::VideoFormat::V210,
    ] {
        let flow_id = uuid::Uuid::new_v4().to_string();
        let domain_guard = common::TestDomainGuard::new("video_interlaced_sink");
        let domain = domain_guard.domain();
        let producer = gst::parse::launch(&format!(
            "appsrc name=src format=time \
               caps=video/x-raw,format={},width={WIDTH},height={HEIGHT},\
                    framerate={FRAMERATE_NUM}/{FRAMERATE_DEN},interlace-mode=interleaved \
               ! mxlsink flow-id={flow_id} domain={domain}",
            format.to_str()
        ))
        .expect("parse producer")
        .downcast::<gst::Pipeline>()
        .expect("producer pipeline");
        let appsrc = producer
            .by_name("src")
            .expect("appsrc")
            .downcast::<gst_app::AppSrc>()
            .expect("AppSrc downcast");

        producer
            .set_state(gst::State::Playing)
            .expect("producer Playing");
        let info = gst_video::VideoInfo::builder(format, WIDTH, HEIGHT)
            .build()
            .expect("VideoInfo");
        let mut buffer = gst::Buffer::with_size(info.size()).expect("buffer");
        buffer
            .get_mut()
            .expect("buffer mut")
            .set_pts(gst::ClockTime::ZERO);
        let _ = appsrc.push_buffer(buffer);

        let bus = producer.bus().expect("bus");
        let error =
            bus.timed_pop_filtered(gst::ClockTime::from_seconds(5), &[gst::MessageType::Error]);
        let _ = producer.set_state(gst::State::Null);
        assert!(error.is_some(), "interlaced {format:?} was accepted");
        let flows = std::fs::read_dir(&domain)
            .map(|dir| dir.filter_map(Result::ok).count())
            .unwrap_or(0);
        assert_eq!(flows, 0, "a flow was created for interlaced {format:?}");
    }
}

/// An interlaced flow written by another application is refused by `mxlsrc`, for
/// `video/v210` and `video/v210a` alike, rather than read as progressive.
#[test]
fn interlaced_flows_are_refused_by_mxlsrc() {
    init();
    const FACTORIES: &[&str] = &["mxlsrc", "fakesink"];
    if let Some(reason) = skip_reason(FACTORIES) {
        skip!(reason);
    }

    for media_type in ["video/v210", "video/v210a"] {
        let domain_guard = common::TestDomainGuard::new("video_interlaced_src");
        let domain = domain_guard.domain();
        let api = mxl::load_api(mxl::config::get_mxl_so_path()).expect("load the MXL API");
        let instance = mxl::MxlInstance::new(api, &domain, "").expect("MXL instance");

        let flow_id = uuid::Uuid::new_v4().to_string();
        let mut components = vec![
            serde_json::json!({"name": "Y", "width": WIDTH, "height": HEIGHT, "bit_depth": 10}),
            serde_json::json!({"name": "Cb", "width": WIDTH / 2, "height": HEIGHT, "bit_depth": 10}),
            serde_json::json!({"name": "Cr", "width": WIDTH / 2, "height": HEIGHT, "bit_depth": 10}),
        ];
        if media_type == "video/v210a" {
            components.push(
                serde_json::json!({"name": "A", "width": WIDTH, "height": HEIGHT, "bit_depth": 10}),
            );
        }
        let flow_def = serde_json::json!({
            "id": flow_id,
            "description": "Interlaced test flow",
            "tags": {"urn:x-nmos:tag:grouphint/v1.0": ["Interlaced test:Video"]},
            "format": "urn:x-nmos:format:video",
            "label": "Interlaced test flow",
            "parents": [],
            "media_type": media_type,
            "grain_rate": {"numerator": FRAMERATE_NUM, "denominator": FRAMERATE_DEN},
            "frame_width": WIDTH,
            "frame_height": HEIGHT,
            "interlace_mode": "interlaced_tff",
            "colorspace": "BT709",
            "components": components,
        });
        // The writer must outlive the reader's pipeline, so it is kept until the end.
        let (_writer, _info, created) = instance
            .create_flow_writer(&flow_def.to_string(), None)
            .expect("create the interlaced flow");
        assert!(created, "{media_type}: flow not created");

        let consumer = gst::parse::launch(&format!(
            "mxlsrc video-flow-id={flow_id} domain={domain} ! fakesink"
        ))
        .expect("parse consumer")
        .downcast::<gst::Pipeline>()
        .expect("consumer pipeline");
        let _ = consumer.set_state(gst::State::Playing);
        let bus = consumer.bus().expect("bus");
        let error =
            bus.timed_pop_filtered(gst::ClockTime::from_seconds(5), &[gst::MessageType::Error]);
        let _ = consumer.set_state(gst::State::Null);
        let error = error.unwrap_or_else(|| panic!("interlaced {media_type} flow was read"));
        let gst::MessageView::Error(error) = error.view() else {
            unreachable!("filtered on errors");
        };
        // The element reports start-up failures with the reason in the debug text.
        let debug = error.debug().map(|d| d.to_string()).unwrap_or_default();
        assert!(
            debug.contains("is interlaced"),
            "{media_type}: refused for another reason: {} ({debug})",
            error.error()
        );
    }
}
