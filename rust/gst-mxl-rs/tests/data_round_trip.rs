// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! GStreamer integration tests for the MXL discrete data flow path.
//!
//! Two end-to-end round trips, both with `mxlsink` and `mxlsrc` connected
//! through a per-test MXL domain under `/dev/shm` torn down on drop:
//!
//! 1. [`st2038_round_trip_via_mxl`]: synthetic ST 2038 ANC packets
//!    pushed via `appsrc`, asserted byte-for-byte at `appsink`.
//! 2. [`cea608_round_trip_via_mxl`]: CEA-608 closed captions from
//!    an SRT file, encoded to ST 2038, sent over MXL, decoded back to
//!    plain text.
//!
//! `TestDomainGuard` mirrors `rust/mxl/tests/basic_tests.rs::TestDomainGuard`.

use std::path::PathBuf;
use std::sync::Once;

use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;

/// ST 2038 ANC packets used by this test, both 100 octets each. Same bytes
/// as `format::data::tests::ST2038_TEST_PACKETS` and `gst-plugin-rtp`
/// `smpte291::tests::packets`. The two packets differ in one user-data word
/// (and therefore checksum) so per-buffer assertions can distinguish them.
const ST2038_TEST_PACKETS: &[&[u8]] = &[
    &[
        0x00, 0x02, 0x40, 0x02, 0x61, 0x80, 0x64, 0x96, 0x59, 0x69, 0x92, 0x64, 0xf9, 0x0e, 0x02,
        0x8f, 0x57, 0x2b, 0xd1, 0xfc, 0xa0, 0x28, 0x0b, 0xf6, 0x80, 0xa0, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0x74, 0x80, 0xa3, 0xd5, 0x06, 0xab,
    ],
    &[
        0x00, 0x02, 0x40, 0x02, 0x61, 0x80, 0x64, 0x96, 0x59, 0x69, 0x92, 0x64, 0xf9, 0x0e, 0x02,
        0x8f, 0x97, 0x2b, 0xd1, 0xfc, 0xa0, 0x28, 0x0b, 0xf6, 0x80, 0xa0, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0xfa, 0x40, 0x10, 0x07, 0xe9, 0x00, 0x40, 0x1f, 0xa4, 0x01, 0x00,
        0x7e, 0x90, 0x04, 0x01, 0x74, 0x80, 0xa3, 0xe4, 0xfe, 0xab,
    ],
];

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

fn make_buffer(bytes: &[u8], pts: gst::ClockTime) -> gst::Buffer {
    let mut buf = gst::Buffer::with_size(bytes.len()).expect("alloc buffer");
    {
        let mut_buf = buf.get_mut().expect("buffer mut");
        mut_buf.copy_from_slice(0, bytes).expect("fill buffer");
        mut_buf.set_pts(pts);
    }
    buf
}

/// Per-test MXL domain under `/dev/shm`, removed on drop.
///
/// Mirrors `rust/mxl/tests/basic_tests.rs::TestDomainGuard`. Linux-only, like
/// the existing GStreamer in-process tests in this crate.
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
        // Best-effort cleanup; don't mask test failures by panicking here.
        let _ = std::fs::remove_dir_all(&self.dir);
    }
}

/// Synthetic ST 2038 round-trip via `appsrc` → `mxlsink` → MXL → `mxlsrc` →
/// `appsink`.
///
/// Push known RFC 8331 / ST 2038 ANC packets into the producer alternately
/// and at integer multiples of the grain period, then assert that the
/// consumer receives exact-byte matches that themselves alternate in the
/// same order once steady state is reached.
#[test]
fn st2038_round_trip_via_mxl() {
    init();

    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new("st2038_round_trip");
    let domain = domain_guard.domain();

    // appsrc `format=time` is required because we set per-buffer PTS and
    // mxlsink syncs to the pipeline clock; without it appsrc's segments
    // would be `GST_FORMAT_BYTES`. appsink `sync=false` because mxlsrc
    // is a live source already emitting grains at the MXL clock rate, so
    // appsink doesn't need to wall-clock-sync as well.
    let producer_desc = format!(
        "appsrc name=src caps=\"meta/x-st-2038,alignment=frame,framerate=30000/1001\" format=time \
         ! queue \
         ! mxlsink flow-id={flow_id} domain={domain}"
    );
    let consumer_desc = format!(
        "mxlsrc data-flow-id={flow_id} domain={domain} \
         ! queue \
         ! appsink name=sink caps=\"meta/x-st-2038,alignment=frame,framerate=30000/1001\" sync=false"
    );

    let producer = gst::parse::launch(&producer_desc)
        .expect("parse producer")
        .downcast::<gst::Pipeline>()
        .expect("producer is Pipeline");
    let consumer = gst::parse::launch(&consumer_desc)
        .expect("parse consumer")
        .downcast::<gst::Pipeline>()
        .expect("consumer is Pipeline");

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

    producer
        .set_state(gst::State::Playing)
        .expect("producer Playing");
    consumer
        .set_state(gst::State::Playing)
        .expect("consumer Playing");

    // Push buffers alternating between the two known ST 2038 packets, with
    // explicit PTS at integer multiples of the grain period, so each push
    // deterministically maps to a distinct MXL index. This makes the
    // alternation property a function of the data path, not of wall-clock
    // jitter on `appsrc`'s synthesised live timestamps. The `mxlsink` writer
    // paces commits against the MXL clock internally, so we don't sleep here.

    // Matches the framerate pinned on both pipelines above (30000/1001).
    const GRAIN_PERIOD_NS: u64 = gst::ClockTime::SECOND.nseconds() * 1_001 / 30_000;
    // How many buffers we collect on the consumer side before asserting.
    const PULL_COUNT: usize = 7;
    // Push a couple more buffers than we're going to pull, because the
    // consumer's first MXL read index can be a grain or two after the
    // producer's first MXL write index depending on scheduler ordering.
    const PUSH_COUNT: usize = PULL_COUNT + 2;
    for i in 0..PUSH_COUNT {
        let pts = gst::ClockTime::from_nseconds(i as u64 * GRAIN_PERIOD_NS);
        let bytes = ST2038_TEST_PACKETS[i % ST2038_TEST_PACKETS.len()];
        appsrc.push_buffer(make_buffer(bytes, pts)).expect("push");
    }

    // Pull samples and assert each is one of the two known packets.
    let mut observed: Vec<usize> = Vec::with_capacity(PULL_COUNT);
    for _ in 0..PULL_COUNT {
        let sample = appsink
            .try_pull_sample(gst::ClockTime::from_seconds(2))
            .expect("appsink produced a sample within 2s");

        let caps = sample.caps().expect("sample caps");
        let s = caps.structure(0).expect("caps structure");
        assert_eq!(s.name(), "meta/x-st-2038");

        let buffer = sample.buffer().expect("sample buffer");
        let map = buffer.map_readable().expect("map readable");
        let bytes = map.as_slice();

        let idx = ST2038_TEST_PACKETS
            .iter()
            .position(|p| *p == bytes)
            .unwrap_or_else(|| {
                panic!(
                    "expected one of the ST 2038 test packets \
                     (len={}, first 16 bytes={:02x?})",
                    bytes.len(),
                    &bytes[..bytes.len().min(16)]
                )
            });
        observed.push(idx);
    }

    assert!(
        observed.windows(2).all(|w| w[0] != w[1]),
        "expected ST 2038 test packets to alternate, got {observed:?}"
    );
    assert!(
        observed.contains(&0) && observed.contains(&1),
        "expected to observe both ST 2038 test packets, got {observed:?}"
    );

    producer.set_state(gst::State::Null).expect("producer Null");
    consumer.set_state(gst::State::Null).expect("consumer Null");
}

/// Asserts that all named GStreamer element factories are registered.
fn require_factories(names: &[&str]) {
    let missing: Vec<&str> = names
        .iter()
        .filter(|n| gst::ElementFactory::find(n).is_none())
        .copied()
        .collect();
    assert!(missing.is_empty(), "missing element factories: {missing:?}");
}

/// SRT → text → CEA-608 → ST 2038 → MXL → ST 2038 → CEA-608 → text round-trip.
///
/// Captions in `tests/data/example.srt` are encoded to CEA-608, wrapped in
/// ST 2038, sent through MXL via `mxlsink` and `mxlsrc`, decoded back to plain
/// text, and the first one asserted to contain "cats". `ccconverter` does the
/// `format=raw` ↔ `format=s334-1a` translation on both sides.
///
/// Requires `ccconverter` from `gstreamer1.0-plugins-bad` and the
/// `cctost2038anc` / `st2038anctocc` / `tttocea608` / `cea608tott` elements
/// from the `rsclosedcaption` plugin in `gst-plugins-rs`. The plugin isn't
/// apt-packaged on Ubuntu 24.04 — `.devcontainer/Dockerfile` builds it
/// from source.
#[test]
fn cea608_round_trip_via_mxl() {
    init();
    require_factories(&[
        "filesrc",
        "subparse",
        "tttocea608",
        "ccconverter",
        "cctost2038anc",
        "st2038anctocc",
        "cea608tott",
        "appsink",
    ]);

    let flow_id = uuid::Uuid::new_v4().to_string();
    let domain_guard = TestDomainGuard::new("cea608_round_trip");
    let domain = domain_guard.domain();
    let srt_path = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/data/example.srt");

    // Non-obvious choices in these pipelines:
    //
    // * `ccconverter` is required on both sides: `tttocea608` and `cea608tott`
    //   speak only `format=raw`; `cctost2038anc` and `st2038anctocc` speak
    //   only `format=s334-1a`.
    // * `closedcaption/x-cea-608, framerate=30000/1001` pins the media type,
    //   without which `ccconverter` would rewrap as CEA-708 CDP (also
    //   accepted by `cctost2038anc`), and pins the framerate upstream of
    //   `ccconverter` (which doesn't rate-convert).
    // * `meta/x-st-2038, alignment=frame, framerate=30000/1001` at the MXL
    //   boundary makes the grain rate explicit on both sides.
    // * `tttocea608 mode=pop-on`: the default `roll-up2` never emits an
    //   "End-Of-Caption" control code, so `cea608tott` has nothing to
    //   commit before EOS and produces no captions.
    // * `text/x-raw, format=utf8` after `cea608tott` picks plain text from
    //   its src template (the default is `application/x-subtitle-vtt`
    //   with a WebVTT header buffer and timestamp-wrapped cues).
    // * `appsink sync=false`: mxlsrc is a live source already emitting
    //   grains at the MXL clock rate, so appsink doesn't need to
    //   wall-clock-sync as well.
    let producer_desc = format!(
        "filesrc location={srt_path} \
         ! subparse \
         ! tttocea608 mode=pop-on \
         ! ccconverter \
         ! closedcaption/x-cea-608,framerate=30000/1001 \
         ! cctost2038anc \
         ! meta/x-st-2038,alignment=frame,framerate=30000/1001 \
         ! mxlsink flow-id={flow_id} domain={domain}"
    );
    let consumer_desc = format!(
        "mxlsrc data-flow-id={flow_id} domain={domain} \
         ! meta/x-st-2038,alignment=frame,framerate=30000/1001 \
         ! st2038anctocc \
         ! ccconverter \
         ! closedcaption/x-cea-608 \
         ! cea608tott \
         ! text/x-raw,format=utf8 \
         ! appsink name=sink sync=false"
    );

    let producer = gst::parse::launch(&producer_desc)
        .expect("parse producer")
        .downcast::<gst::Pipeline>()
        .expect("producer is Pipeline");
    let consumer = gst::parse::launch(&consumer_desc)
        .expect("parse consumer")
        .downcast::<gst::Pipeline>()
        .expect("consumer is Pipeline");

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

    // With the text/x-raw capsfilter above, each appsink buffer is exactly
    // one decoded caption, and every caption in example.srt contains "cats",
    // so the first sample we receive should already match — even if mxlsrc's
    // startup skew makes us miss some captions. Wall-clock budget more than
    // covers producer pacing (cea608tott emits each caption around its end
    // PTS as the EOC arrives, so first caption at ~0.5s, second at ~1.0s, etc.,
    // mxlsink syncs to the pipeline clock) plus consumer-side pipeline latency.
    let sample = appsink
        .try_pull_sample(gst::ClockTime::from_seconds(2))
        .expect("text sample within 2s");

    let caps_name = sample
        .caps()
        .and_then(|c| c.structure(0).map(|s| s.name().to_string()));
    assert_eq!(
        caps_name.as_deref(),
        Some("text/x-raw"),
        "expected text/x-raw caps after cea608tott, got {caps_name:?}"
    );

    let buffer = sample.buffer().expect("sample buffer");
    let map = buffer.map_readable().expect("map readable");
    let caption =
        std::str::from_utf8(map.as_slice()).expect("appsink text/x-raw buffer should be UTF-8");
    assert!(
        caption.contains("cats"),
        "expected first caption to contain \"cats\" from example.srt, got {caption:?}"
    );

    producer.set_state(gst::State::Null).expect("producer Null");
    consumer.set_state(gst::State::Null).expect("consumer Null");
}
