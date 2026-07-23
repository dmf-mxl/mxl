// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Shared helpers for the gst-mxl-rs GStreamer integration tests.
//!
//! The tests round-trip real buffers through `mxlsink` → MXL → `mxlsrc`, so they
//! need Linux `/dev/shm` (tmpfs, for MXL's `mkdtemp(3)` domains) and the
//! `st2038extractor`/`st2038combiner` elements from `gst-plugins-rs`. Rather than
//! `#[ignore]` (which hides them from every run), each test calls [`skip_reason`]
//! and, when a prerequisite is missing, [`skip!`]s — printing a libtest-style
//! `... skipped, <reason>` line that CI can surface as a notice.

// Each integration test binary pulls in this module but uses a different subset.
#![allow(dead_code)]

use std::path::PathBuf;
use std::sync::Once;

use gst::prelude::*;
use gstreamer as gst;
use gstreamer_app as gst_app;
use gstreamer_video as gst_video;

pub const FRAMERATE_NUM: i32 = 30_000;
pub const FRAMERATE_DEN: i32 = 1_001;
pub const FRAME_PERIOD_NS: u64 =
    gst::ClockTime::SECOND.nseconds() * FRAMERATE_DEN as u64 / FRAMERATE_NUM as u64;
// v210 pads lines to a multiple of 128 bytes; width >= 2 is valid.
pub const VIDEO_WIDTH: u32 = 2;
pub const VIDEO_HEIGHT: u32 = 2;

const SHM_MXL_HINT: &str = "MXL uses mkdtemp(3) under /dev/shm; run integration tests \
    on Linux with tmpfs (/dev/shm)";

/// Print a libtest-style `test <name> ... skipped, <reason>` line and return
/// from the calling test. Mirrors the line a static `#[ignore = "..."]` emits,
/// so a skipped prerequisite is visible (under `--nocapture`) instead of hidden.
///
/// Only usable from a test that returns `()`, since it expands to `return;`.
#[macro_export]
macro_rules! skip {
    ($reason:expr $(,)?) => {{
        let thread = ::std::thread::current();
        let name = thread.name().unwrap_or("<unknown test>");
        ::std::eprintln!("test {name} ... skipped, {}", $reason);
        return;
    }};
}

static REGISTER: Once = Once::new();

/// Initialise GStreamer and register `mxlsrc`/`mxlsink` once per process.
pub fn init() {
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

/// Reason these integration tests cannot run here, or `None` when the
/// prerequisites are present. Checks (in order) that MXL's `/dev/shm` domains
/// work on this platform, then that every named element factory is registered.
/// Call [`init`] first so the factory lookup sees the registered elements.
pub fn skip_reason(factories: &[&str]) -> Option<String> {
    if let Some(reason) = dev_shm_skip_reason() {
        return Some(reason);
    }
    let missing: Vec<&str> = factories
        .iter()
        .filter(|n| gst::ElementFactory::find(n).is_none())
        .copied()
        .collect();
    if !missing.is_empty() {
        return Some(format!(
            "missing element factories: {missing:?}; set GST_PLUGIN_PATH to include the \
             gst-mxl-rs build and the gst-plugins-rs closedcaption plugin"
        ));
    }
    None
}

/// `Some(reason)` when MXL's `mkdtemp(3)` domains under `/dev/shm` are
/// unavailable (non-Linux, or a sandbox blocking tmpfs); `None` when usable.
#[cfg(target_os = "linux")]
fn dev_shm_skip_reason() -> Option<String> {
    use std::ffi::CStr;

    let mut path = b"/dev/shm/mxl_gst_shm_probeXXXXXX\0".to_vec();
    let created = unsafe {
        unsafe extern "C" {
            fn mkdtemp(template: *mut i8) -> *mut i8;
        }
        mkdtemp(path.as_mut_ptr() as *mut i8)
    };
    if created.is_null() {
        let err = std::io::Error::last_os_error();
        return Some(format!(
            "mkdtemp(3) on /dev/shm failed: {err}; {SHM_MXL_HINT}"
        ));
    }
    let dir = unsafe { CStr::from_ptr(created) };
    std::fs::remove_dir_all(dir.to_str().expect("mkdtemp path")).ok();
    None
}

#[cfg(not(target_os = "linux"))]
fn dev_shm_skip_reason() -> Option<String> {
    Some(format!("not Linux; {SHM_MXL_HINT}"))
}

/// Per-test MXL domain under `/dev/shm`, removed on drop.
pub struct TestDomainGuard {
    dir: PathBuf,
}

impl TestDomainGuard {
    pub fn new(test: &str) -> Self {
        let dir = PathBuf::from(format!(
            "/dev/shm/mxl_gst_test_domain_{test}_{}",
            uuid::Uuid::new_v4()
        ));
        std::fs::create_dir_all(&dir).unwrap_or_else(|e| {
            panic!("create test domain {}: {e}\n{SHM_MXL_HINT}", dir.display())
        });
        Self { dir }
    }

    pub fn domain(&self) -> String {
        self.dir.to_string_lossy().into_owned()
    }
}

impl Drop for TestDomainGuard {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.dir);
    }
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

/// Attach one `GstAncillaryMeta` (SMPTE 291 DID/SDID + payload) to `buffer`,
/// extending each octet to a 10-bit even/odd-parity word and computing the
/// checksum, as `st2038extractor` expects on the wire.
pub fn add_ancillary_meta(
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

/// `parse::launch` the shared producer (`appsrc` → `st2038extractor` → a video
/// `mxlsink` and a companion data `mxlsink`) and return the pipeline, its
/// `appsrc`, and the v210 frame size in bytes.
pub fn build_producer(
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

/// Producer frame stamp in byte 0 of the first ST 2038 ANC packet's user data.
pub fn st2038_first_packet_data0(st2038: &[u8]) -> u8 {
    let mut rem = st2038;
    let meta = gstmxl::format::data::ancillary_meta_from_st2038_anc_packet(&mut rem)
        .expect("parse first ST 2038 ANC");
    (*meta.data.first().expect("ST 2038 data_count was zero") & 0xff) as u8
}

/// Collect any bus errors currently queued on `pipeline`.
pub fn collect_bus_errors(pipeline: &gst::Pipeline) -> Vec<String> {
    let Some(bus) = pipeline.bus() else {
        return Vec::new();
    };
    let mut errors = Vec::new();
    while let Some(msg) = bus.pop() {
        if let gst::MessageView::Error(err) = msg.view() {
            errors.push(format!(
                "{:?}: {} ({:?})",
                err.src().map(|s| s.path_string()),
                err.error(),
                err.debug()
            ));
        }
    }
    errors
}

/// Panic with a readable summary if `errors` is non-empty.
pub fn assert_bus_no_errors(label: &str, errors: &[String]) {
    if errors.is_empty() {
        return;
    }
    let shm = errors
        .iter()
        .any(|e| e.contains("mkdtemp") || e.contains("ENOENT") || e.contains("Flow not found"));
    let hint = if shm {
        format!("\n{SHM_MXL_HINT}")
    } else {
        String::new()
    };
    panic!("{label} GStreamer bus errors:{hint}\n{}", errors.join("\n"));
}
