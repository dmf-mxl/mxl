// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

mod common;

use common::{TestDomainGuard, init, skip_reason};
use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;
use mxl::{MxlInstance, config::get_mxl_so_path};

const SAMPLE_RATE: u64 = 48_000;
const CHANNELS: usize = 2;
const SAMPLES_PER_BUFFER: u64 = 1024;
const BUFFERS: u64 = 50;

fn ms_rounded_pts(k: u64) -> gst::ClockTime {
    let ms = (k * SAMPLES_PER_BUFFER * 1000 + SAMPLE_RATE / 2) / SAMPLE_RATE;
    gst::ClockTime::from_mseconds(ms)
}

fn counter_buffer(k: u64) -> gst::Buffer {
    let first = k * SAMPLES_PER_BUFFER;
    let mut data = Vec::with_capacity(SAMPLES_PER_BUFFER as usize * CHANNELS * 4);
    for n in first..first + SAMPLES_PER_BUFFER {
        for _ in 0..CHANNELS {
            data.extend_from_slice(&(n as f32).to_le_bytes());
        }
    }
    let mut buffer = gst::Buffer::from_mut_slice(data);
    {
        let b = buffer.get_mut().expect("buffer mut");
        b.set_pts(ms_rounded_pts(k));
        b.set_duration(gst::ClockTime::from_nseconds(
            SAMPLES_PER_BUFFER * gst::ClockTime::SECOND.nseconds() / SAMPLE_RATE,
        ));
    }
    buffer
}

fn wait_for_eos(pipeline: &gst::Pipeline, timeout: gst::ClockTime, label: &str) {
    let bus = pipeline.bus().expect("bus");
    let msg = bus
        .timed_pop_filtered(timeout, &[gst::MessageType::Eos, gst::MessageType::Error])
        .unwrap_or_else(|| panic!("{label}: timed out waiting for EOS"));
    if let gst::MessageView::Error(err) = msg.view() {
        panic!(
            "{label} error from {:?}: {} ({:?})",
            err.src().map(|s| s.path_string()),
            err.error(),
            err.debug()
        );
    }
}

fn read_flow_tail(domain: &str, flow_id: &str, max: usize) -> Vec<Vec<f32>> {
    let api = mxl::load_api(get_mxl_so_path()).expect("load MXL API");
    let instance = MxlInstance::new(api, domain, "").expect("MXL instance");
    let reader = instance
        .create_flow_reader(flow_id)
        .expect("flow reader")
        .to_samples_reader()
        .expect("samples reader");
    let head = reader.get_runtime_info().expect("runtime info").headIndex;
    let buffer_length = reader
        .get_config_info()
        .expect("config info")
        .continuous()
        .expect("continuous config")
        .bufferLength as usize;
    let count = (buffer_length / 2).min(max);
    let samples = reader
        .get_samples_non_blocking(head, count)
        .expect("read samples before head");
    (0..samples.num_of_channels())
        .map(|ch| {
            let (first, second) = samples.channel_data(ch).expect("channel data");
            first
                .chunks_exact(4)
                .chain(second.chunks_exact(4))
                .map(|b| f32::from_le_bytes(b.try_into().expect("4-byte f32")))
                .collect()
        })
        .collect()
}

#[test]
fn millisecond_rounded_audio_timestamps_write_contiguous_samples() {
    init();
    if let Some(reason) = skip_reason(&["appsrc", "mxlsink"]) {
        skip!(reason);
    }
    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new("audio_ms_timestamps");
    let domain = domain_guard.domain();

    let desc = format!(
        "appsrc name=src format=time \
           caps=audio/x-raw,format=F32LE,layout=interleaved,rate={SAMPLE_RATE},\
                channels={CHANNELS},channel-mask=(bitmask)0x3 \
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

    pipeline
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    for k in 0..BUFFERS {
        appsrc.push_buffer(counter_buffer(k)).expect("push buffer");
    }
    appsrc.end_of_stream().expect("end of stream");
    wait_for_eos(&pipeline, gst::ClockTime::from_seconds(5), "producer");

    let channels = read_flow_tail(&domain, &flow_id, (BUFFERS * SAMPLES_PER_BUFFER) as usize);
    assert_eq!(channels.len(), CHANNELS);
    for (ch, values) in channels.iter().enumerate() {
        // The last sample pushed is the last one in the flow.
        assert_eq!(
            values.last().copied(),
            Some((BUFFERS * SAMPLES_PER_BUFFER - 1) as f32),
            "channel {ch}: last sample in the flow is not the last one pushed"
        );
        for (i, pair) in values.windows(2).enumerate() {
            assert_eq!(
                pair[1],
                pair[0] + 1.0,
                "channel {ch}: samples {} and {} in the read window are not consecutive",
                i,
                i + 1
            );
        }
    }

    pipeline.set_state(gst::State::Null).expect("producer Null");
}

const AAC_MKV_PATH: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/tests/data/aac_ms_timestamps.mkv"
);

/// decode and convert to F32LE stereo
const AAC_MKV_DECODE: &str = "matroskademux ! decodebin ! audioconvert \
     ! audio/x-raw,format=F32LE,layout=interleaved,channels=2,channel-mask=(bitmask)0x3";

fn have_aac_decoder() -> bool {
    let aac = gst::Caps::builder("audio/mpeg")
        .field("mpegversion", 4i32)
        .field("stream-format", "raw")
        .build();
    gst::ElementFactory::factories_with_type(
        gst::ElementFactoryType::DECODER | gst::ElementFactoryType::MEDIA_AUDIO,
        gst::Rank::MARGINAL,
    )
    .iter()
    .any(|f| f.can_sink_any_caps(&aac))
}

/// decode the (problematic) aac mkv reference file
fn decode_aac_mkv_reference() -> Vec<Vec<f32>> {
    let desc = format!(
        "filesrc location={AAC_MKV_PATH} ! {AAC_MKV_DECODE} ! appsink name=sink sync=false"
    );
    let pipeline = gst::parse::launch(&desc)
        .expect("parse reference pipeline")
        .downcast::<gst::Pipeline>()
        .expect("reference pipeline");
    let appsink = pipeline
        .by_name("sink")
        .expect("appsink")
        .downcast::<gst_app::AppSink>()
        .expect("AppSink downcast");
    pipeline
        .set_state(gst::State::Playing)
        .expect("reference Playing");

    let mut channels = vec![Vec::new(); CHANNELS];
    while let Ok(sample) = appsink.pull_sample() {
        let buffer = sample.buffer().expect("sample buffer");
        let map = buffer.map_readable().expect("map buffer");
        for frame in map.as_slice().chunks_exact(4 * CHANNELS) {
            for (ch, b) in frame.chunks_exact(4).enumerate() {
                channels[ch].push(f32::from_le_bytes(b.try_into().expect("4-byte f32")));
            }
        }
    }
    wait_for_eos(&pipeline, gst::ClockTime::from_seconds(5), "reference");
    pipeline
        .set_state(gst::State::Null)
        .expect("reference Null");
    channels
}

#[test]
fn aac_in_matroska_writes_all_decoded_samples() {
    init();
    let factories = [
        "filesrc",
        "matroskademux",
        "decodebin",
        "audioconvert",
        "appsink",
        "mxlsink",
    ];
    if let Some(reason) = skip_reason(&factories) {
        skip!(reason);
    }
    if !have_aac_decoder() {
        skip!("no AAC decoder registered");
    }
    let reference = decode_aac_mkv_reference();
    let total = reference[0].len();
    assert!(
        total > 40_000,
        "reference decode produced only {total} samples"
    );

    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new("aac_mkv");
    let domain = domain_guard.domain();
    let desc = format!(
        "filesrc location={AAC_MKV_PATH} ! {AAC_MKV_DECODE} \
         ! mxlsink flow-id={flow_id} domain={domain}"
    );
    let pipeline = gst::parse::launch(&desc)
        .expect("parse producer")
        .downcast::<gst::Pipeline>()
        .expect("producer pipeline");
    pipeline
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    wait_for_eos(&pipeline, gst::ClockTime::from_seconds(10), "producer");

    let flow = read_flow_tail(&domain, &flow_id, total);
    assert_eq!(flow.len(), CHANNELS);
    for (ch, values) in flow.iter().enumerate() {
        // Long enough to cover several 1024-sample AAC frame boundaries.
        assert!(
            values.len() >= 4 * SAMPLES_PER_BUFFER as usize,
            "channel {ch}: read window of {} samples is too short",
            values.len()
        );
        let expected = &reference[ch][total - values.len()..];
        if let Some(i) = values.iter().zip(expected).position(|(a, b)| a != b) {
            panic!(
                "channel {ch}: flow differs from the decoded file at sample {} of the last {} \
                 (flow {}, decoded {})",
                i,
                values.len(),
                values[i],
                expected[i]
            );
        }
    }

    pipeline.set_state(gst::State::Null).expect("producer Null");
}
