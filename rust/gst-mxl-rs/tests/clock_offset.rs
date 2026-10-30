// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Integration tests for the MXL-time/pipeline-clock offset `D` that the MXL
//! elements of one pipeline share.

use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::{Duration, Instant};

#[macro_use]
mod common;

use common::{TestDomainGuard, init, skip_reason};
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
