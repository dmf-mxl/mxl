// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Integration tests for how `mxlsink` writes the caps colorimetry to the flow
//! definition, how `mxlsrc` builds the caps colorimetry from it, and how each
//! refuses colorimetry that the other side cannot express.

use std::collections::HashMap;

#[macro_use]
mod common;

use common::{TestDomainGuard, assert_bus_no_errors, collect_bus_errors, init, skip_reason};
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;
use mxl::{
    MxlInstance,
    config::get_mxl_so_path,
    flowdef::{Component, FlowDef, FlowDefDetails, FlowDefVideo, InterlaceMode, Rate},
};

const VIDEO_CAPS: &str = "video/x-raw,format=v210,width=16,height=16,framerate=25/1";

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

/// The first `ERROR` on the bus of `pipeline`, or a panic if it reaches EOS or
/// posts nothing within the budget.
fn expect_bus_error(pipeline: &gst::Pipeline) -> String {
    let message = pipeline
        .bus()
        .expect("bus")
        .timed_pop_filtered(
            gst::ClockTime::from_seconds(5),
            &[gst::MessageType::Eos, gst::MessageType::Error],
        )
        .expect("no ERROR or EOS within 5 s");
    match message.view() {
        gst::MessageView::Error(err) => format!("{} ({:?})", err.error(), err.debug()),
        _ => panic!("expected ERROR, got {:?}", message.type_()),
    }
}

/// `bt2100-hlg` caps → `mxlsink` → flow definition → `mxlsrc` → the same caps.
#[test]
fn bt2100_hlg_round_trip_via_mxl() {
    init();
    if let Some(reason) = skip_reason(&["appsink", "mxlsink", "mxlsrc", "videotestsrc"]) {
        skip!(reason);
    }
    round_trip("bt2100-hlg", ["BT2100", "HLG", "NARROW"]);
}

/// Full-range BT.709 caps → `mxlsink` → flow definition → `mxlsrc` → the same caps.
#[test]
fn full_range_round_trip_via_mxl() {
    init();
    if let Some(reason) = skip_reason(&["appsink", "mxlsink", "mxlsrc", "videotestsrc"]) {
        skip!(reason);
    }
    round_trip("1:3:5:1", ["BT709", "SDR", "FULL"]);
}

/// Write `colorimetry` caps through `mxlsink`, check the `colorspace`,
/// `transfer_characteristic` and `component_range` in the flow definition, and
/// check that `mxlsrc` produces the same caps colorimetry.
fn round_trip(colorimetry: &str, flow_attributes: [&str; 3]) {
    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new("video_colorimetry_round_trip");
    let domain = domain_guard.domain();

    let producer = launch(&format!(
        "videotestsrc is-live=true \
           ! {VIDEO_CAPS},colorimetry={colorimetry} \
           ! mxlsink flow-id={flow_id} domain={domain}"
    ));
    let consumer = launch(&format!(
        "mxlsrc video-flow-id={flow_id} domain={domain} \
           ! appsink name=sink sync=false"
    ));
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

    let sample = appsink.try_pull_sample(gst::ClockTime::from_seconds(5));
    let flow_def = mxl_instance(&domain).get_flow_def(&flow_id);

    let consumer_errors = collect_bus_errors(&consumer);
    let producer_errors = collect_bus_errors(&producer);
    consumer.set_state(gst::State::Null).expect("consumer Null");
    producer.set_state(gst::State::Null).expect("producer Null");
    assert_bus_no_errors("consumer", &consumer_errors);
    assert_bus_no_errors("producer", &producer_errors);

    let flow_def: serde_json::Value =
        serde_json::from_str(&flow_def.expect("flow definition")).expect("flow definition JSON");
    assert_eq!(
        [
            &flow_def["colorspace"],
            &flow_def["transfer_characteristic"],
            &flow_def["component_range"],
        ],
        flow_attributes
    );

    let sample = sample.expect("mxlsrc delivered no sample within 5 s");
    let caps = sample.caps().expect("sample caps");
    assert_eq!(
        caps.structure(0)
            .expect("caps structure")
            .get::<String>("colorimetry")
            .as_deref(),
        Ok(colorimetry)
    );
}

/// BT.601 with BT.470BG primaries has no flow definition colorspace, so
/// `mxlsink` refuses it.
#[test]
fn mxlsink_refuses_bt470bg_caps() {
    init();
    if let Some(reason) = skip_reason(&["mxlsink", "videotestsrc"]) {
        skip!(reason);
    }
    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new("video_colorimetry_bt470bg");
    let domain = domain_guard.domain();

    let producer = launch(&format!(
        "videotestsrc num-buffers=1 \
           ! {VIDEO_CAPS},colorimetry=2:4:5:3 \
           ! mxlsink flow-id={flow_id} domain={domain}"
    ));
    producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    let error = expect_bus_error(&producer);
    producer.set_state(gst::State::Null).expect("producer Null");
    assert!(
        error.contains("not-negotiated"),
        "unexpected error: {error}"
    );
}

/// An `XYZ` flow has no GStreamer colorimetry, so `mxlsrc` refuses it.
#[test]
fn mxlsrc_refuses_xyz_flow() {
    init();
    if let Some(reason) = skip_reason(&["fakesink", "mxlsrc"]) {
        skip!(reason);
    }
    let flow_id = uuid::Uuid::new_v4();
    let domain_guard = TestDomainGuard::new("video_colorimetry_xyz");
    let domain = domain_guard.domain();

    let component = |name: &str, width| Component {
        name: name.into(),
        width,
        height: 16,
        bit_depth: 10,
    };
    let flow_def = FlowDef {
        id: flow_id,
        description: "XYZ flow".into(),
        tags: HashMap::from([(
            "urn:x-nmos:tag:grouphint/v1.0".to_string(),
            vec!["test:XYZ Video".to_string()],
        )]),
        label: "XYZ flow".into(),
        parents: vec![],
        media_type: "video/v210".into(),
        details: FlowDefDetails::Video(FlowDefVideo {
            grain_rate: Rate {
                numerator: 25,
                denominator: 1,
            },
            frame_width: 16,
            frame_height: 16,
            interlace_mode: InterlaceMode::Progressive,
            colorspace: "XYZ".into(),
            transfer_characteristic: Some("ST428-1".into()),
            component_range: None,
            components: vec![component("Y", 16), component("Cb", 8), component("Cr", 8)],
        }),
    };
    let instance = mxl_instance(&domain);
    let (_writer, _, is_created) = instance
        .create_flow_writer(&serde_json::to_string(&flow_def).unwrap(), None)
        .expect("create flow writer");
    assert!(is_created);

    let consumer = launch(&format!(
        "mxlsrc video-flow-id={flow_id} domain={domain} ! fakesink"
    ));
    consumer
        .set_state(gst::State::Playing)
        .expect("consumer Playing");
    let error = expect_bus_error(&consumer);
    consumer.set_state(gst::State::Null).expect("consumer Null");
    assert!(error.contains("XYZ"), "unexpected error: {error}");
}
