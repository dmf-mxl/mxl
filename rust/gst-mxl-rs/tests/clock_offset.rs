// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Integration tests for the MXL-time/pipeline-clock offset `D` that the MXL
//! elements of one pipeline share.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Barrier};
use std::thread;
use std::time::{Duration, Instant};

#[macro_use]
mod common;

use common::{TestDomainGuard, init, skip_reason};
use gst::glib;
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;
use mxl::{MxlInstance, Rational, config::get_mxl_so_path};

const SAMPLE_RATE: Rational = Rational {
    numerator: 48_000,
    denominator: 1,
};
const SAMPLES: u64 = 480;

fn mxl_instance(domain: &str) -> MxlInstance {
    let api = mxl::load_api(get_mxl_so_path()).expect("load MXL API");
    MxlInstance::new(api, domain, "").expect("MXL instance")
}

fn spin(duration: Duration) {
    let until = Instant::now() + duration;
    while Instant::now() < until {}
}

/// Another thread holding `mxlsink`'s object lock while the sink samples `D`
/// must not skew `D`: MXL time and the pipeline clock are read back-to-back.
#[test]
fn clock_offset_is_not_skewed_by_object_lock_contention() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("offset_contention");
    let domain = domain_guard.domain();
    let instance = mxl_instance(&domain);

    for run in 0..5 {
        let flow_id = uuid::Uuid::new_v4().to_string();
        let pipeline = gst::parse::launch(&format!(
            "appsrc name=src format=time \
               caps=audio/x-raw,format=F32LE,layout=interleaved,rate=48000,\
                 channels=1,channel-mask=(bitmask)0x1 \
             ! mxlsink name=sink flow-id={flow_id} domain={domain}"
        ))
        .expect("parse pipeline")
        .downcast::<gst::Pipeline>()
        .expect("pipeline");
        let appsrc = pipeline
            .by_name("src")
            .expect("appsrc")
            .downcast::<gst_app::AppSrc>()
            .expect("AppSrc downcast");
        let sink = pipeline.by_name("sink").expect("mxlsink");
        pipeline
            .set_state(gst::State::Playing)
            .expect("pipeline Playing");

        // Hold the sink's object lock almost continuously until the buffer has
        // been rendered, so any object-lock access during sampling stalls.
        let stop = Arc::new(AtomicBool::new(false));
        let contender = {
            let sink = sink.clone();
            let stop = stop.clone();
            thread::spawn(move || {
                while !stop.load(Ordering::SeqCst) {
                    {
                        let _lock = sink.object_lock();
                        spin(Duration::from_millis(20));
                    }
                    spin(Duration::from_micros(50));
                }
            })
        };

        let mut buffer = gst::Buffer::with_size(SAMPLES as usize * 4).expect("buffer");
        buffer
            .get_mut()
            .expect("buffer mut")
            .set_pts(gst::ClockTime::ZERO);
        appsrc.push_buffer(buffer).expect("push buffer");
        appsrc.end_of_stream().expect("end of stream");
        let msg = pipeline
            .bus()
            .expect("bus")
            .timed_pop_filtered(
                gst::ClockTime::from_seconds(5),
                &[gst::MessageType::Eos, gst::MessageType::Error],
            )
            .expect("timed out waiting for EOS");
        stop.store(true, Ordering::SeqCst);
        contender.join().expect("contender thread");
        if let gst::MessageView::Error(err) = msg.view() {
            panic!("run {run}: {} ({:?})", err.error(), err.debug());
        }

        let clock = pipeline.clock().expect("pipeline clock");
        let before = clock.time().nseconds() as i128;
        let mxl_now = instance.get_time() as i128;
        let after = clock.time().nseconds() as i128;
        let measured = mxl_now - (before + after) / 2;

        let reader = instance
            .create_flow_reader(&flow_id)
            .expect("flow reader")
            .to_samples_reader()
            .expect("samples reader");
        let first = reader.get_runtime_info().expect("runtime info").headIndex - SAMPLES;
        let used = instance
            .index_to_timestamp(first, &SAMPLE_RATE)
            .expect("index to timestamp") as i128
            - sink.base_time().expect("base time").nseconds() as i128;
        drop(reader);
        pipeline.set_state(gst::State::Null).expect("pipeline Null");

        let error_us = (measured - used) / 1_000;
        assert!(
            error_us.abs() < 1_000,
            "run {run}: sink D differs from the measured offset by {error_us} us"
        );
    }
}

/// Two `mxlsink`s of one pipeline that start on different threads at the same
/// time must still share one offset, so exactly one of them publishes it.
#[test]
fn concurrently_started_elements_share_one_clock_offset() {
    init();
    if let Some(reason) = skip_reason(&["mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("offset_concurrent_start");
    let domain = domain_guard.domain();

    for run in 0..50 {
        let pipeline = gst::Pipeline::new();
        let sinks: Vec<gst::Element> = (0..2)
            .map(|_| {
                gst::ElementFactory::make("mxlsink")
                    .property("flow-id", uuid::Uuid::new_v4().to_string())
                    .property("domain", &domain)
                    .build()
                    .expect("mxlsink")
            })
            .collect();
        for sink in &sinks {
            pipeline.add(sink).expect("add mxlsink");
            sink.set_locked_state(true);
        }
        pipeline
            .set_state(gst::State::Ready)
            .expect("pipeline Ready");

        let barrier = Arc::new(Barrier::new(sinks.len()));
        let starters: Vec<_> = sinks
            .iter()
            .cloned()
            .map(|sink| {
                let barrier = barrier.clone();
                thread::spawn(move || {
                    barrier.wait();
                    sink.set_state(gst::State::Ready).expect("mxlsink Ready");
                })
            })
            .collect();
        for starter in starters {
            starter.join().expect("starter thread");
        }

        let bus = pipeline.bus().expect("bus");
        let published =
            std::iter::from_fn(|| bus.pop_filtered(&[gst::MessageType::HaveContext])).count();

        for sink in &sinks {
            sink.set_locked_state(false);
        }
        pipeline.set_state(gst::State::Null).expect("pipeline Null");

        assert_eq!(published, 1, "run {run}: offsets published by the sinks");
    }
}

/// Removing an element from a running pipeline must not make its former
/// siblings sample the offset again. The pipeline clock jumps forward between
/// two writes, so a second sample would move the remaining sink's next write
/// back over samples it has already committed.
#[test]
fn removed_running_element_keeps_siblings_clock_offset() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("offset_removed_running");
    let domain = domain_guard.domain();
    let instance = mxl_instance(&domain);

    let flow_id = uuid::Uuid::new_v4().to_string();
    let pipeline = gst::parse::launch(&format!(
        "appsrc name=src_a format=time \
           caps=audio/x-raw,format=F32LE,layout=interleaved,rate=48000,\
             channels=1,channel-mask=(bitmask)0x1 \
         ! mxlsink name=sink_a flow-id={flow_id} domain={domain} \
         appsrc name=src_b format=time \
           caps=audio/x-raw,format=F32LE,layout=interleaved,rate=48000,\
             channels=1,channel-mask=(bitmask)0x1 \
         ! mxlsink name=sink_b flow-id={} domain={domain}",
        uuid::Uuid::new_v4()
    ))
    .expect("parse pipeline")
    .downcast::<gst::Pipeline>()
    .expect("pipeline");
    let clock: gst::Clock = glib::Object::builder::<gst::SystemClock>().build().upcast();
    pipeline.use_clock(Some(&clock));
    let appsrc = |name| {
        pipeline
            .by_name(name)
            .expect("appsrc")
            .downcast::<gst_app::AppSrc>()
            .expect("AppSrc downcast")
    };
    let (src_a, src_b) = (appsrc("src_a"), appsrc("src_b"));
    let buffer = |pts| {
        let mut buffer = gst::Buffer::with_size(SAMPLES as usize * 4).expect("buffer");
        buffer.get_mut().expect("buffer mut").set_pts(pts);
        buffer
    };

    pipeline
        .set_state(gst::State::Playing)
        .expect("pipeline Playing");
    src_a
        .push_buffer(buffer(gst::ClockTime::ZERO))
        .expect("push");
    src_b
        .push_buffer(buffer(gst::ClockTime::ZERO))
        .expect("push");
    pipeline
        .state(gst::ClockTime::from_seconds(5))
        .0
        .expect("pipeline Playing");

    let reader = instance
        .create_flow_reader(&flow_id)
        .expect("flow reader")
        .to_samples_reader()
        .expect("samples reader");
    let deadline = Instant::now() + Duration::from_secs(5);
    while reader.get_runtime_info().expect("runtime info").headIndex == u64::MAX {
        assert!(Instant::now() < deadline, "first buffer was not written");
        thread::sleep(Duration::from_millis(1));
    }
    drop(reader);

    let sink_b = pipeline.by_name("sink_b").expect("mxlsink");
    pipeline.remove(&sink_b).expect("remove running sink");
    let (internal, external, num, denom) = clock.calibration();
    clock.set_calibration(
        internal,
        external + gst::ClockTime::from_seconds(1),
        num,
        denom,
    );

    src_a
        .push_buffer(buffer(gst::ClockTime::from_mseconds(10)))
        .expect("push");
    src_a.end_of_stream().expect("end of stream");
    let msg = pipeline
        .bus()
        .expect("bus")
        .timed_pop_filtered(
            gst::ClockTime::from_seconds(5),
            &[gst::MessageType::Eos, gst::MessageType::Error],
        )
        .expect("timed out waiting for EOS");
    pipeline.set_state(gst::State::Null).expect("pipeline Null");
    sink_b
        .set_state(gst::State::Null)
        .expect("removed sink Null");

    if let gst::MessageView::Error(err) = msg.view() {
        panic!("{} ({:?})", err.error(), err.debug());
    }
}

/// An element moved into another pipeline must take that pipeline's offset,
/// not keep the one it shared in the pipeline it came from.
#[test]
fn moved_element_takes_new_pipeline_clock_offset() {
    init();
    if let Some(reason) = skip_reason(&["videotestsrc", "mxlsink"]) {
        skip!(reason);
    }
    let domain_guard = TestDomainGuard::new("offset_moved_element");
    let domain = domain_guard.domain();
    let instance = mxl_instance(&domain);
    let grain_rate = Rational {
        numerator: 25,
        denominator: 1,
    };

    let flows = [
        uuid::Uuid::new_v4().to_string(),
        uuid::Uuid::new_v4().to_string(),
    ];
    let branches: Vec<gst::Bin> = flows
        .iter()
        .map(|flow_id| {
            gst::parse::bin_from_description(
                &format!(
                    "videotestsrc is-live=true \
                     ! video/x-raw,format=v210,width=2,height=2,framerate=25/1 \
                     ! mxlsink flow-id={flow_id} domain={domain}"
                ),
                false,
            )
            .expect("parse branch")
        })
        .collect();

    // The first pipeline runs on the realtime clock, so its offset is far from
    // the offset against the default monotonic clock of the second pipeline.
    let first = gst::Pipeline::new();
    let realtime: gst::Clock = glib::Object::builder::<gst::SystemClock>()
        .property("clock-type", gst::ClockType::Realtime)
        .build()
        .upcast();
    first.use_clock(Some(&realtime));
    first.add_many(&branches).expect("add branches");
    first.set_state(gst::State::Playing).expect("first Playing");
    thread::sleep(Duration::from_millis(500));

    branches[1]
        .set_state(gst::State::Null)
        .expect("moved branch Null");
    first.remove(&branches[1]).expect("remove moved branch");
    let second = gst::Pipeline::new();
    second.add(&branches[1]).expect("add moved branch");
    second
        .set_state(gst::State::Playing)
        .expect("second Playing");
    thread::sleep(Duration::from_millis(500));

    let reader = instance
        .create_flow_reader(&flows[1])
        .expect("flow reader")
        .to_grain_reader()
        .expect("grain reader");
    let head = reader.get_runtime_info().expect("runtime info").headIndex as i64;
    let now = instance.get_current_index(&grain_rate) as i64;
    drop(reader);

    second.set_state(gst::State::Null).expect("second Null");
    first.set_state(gst::State::Null).expect("first Null");

    assert!(
        (head - now).abs() <= 5,
        "moved element wrote {} grains away from MXL now",
        head - now
    );
}
