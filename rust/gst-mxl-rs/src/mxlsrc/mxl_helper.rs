// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{
    sync::{LazyLock, MutexGuard},
    time::Duration,
};

use glib::subclass::types::ObjectSubclassExt;
use gst_base::prelude::*;
use gstreamer as gst;
use gstreamer_base as gst_base;
use mxl::{FlowReader, MxlInstance, config::get_mxl_so_path, flowdef::*};

use crate::mxlsrc::{
    imp::*,
    state::{ContinuousState, DiscreteFormat, DiscreteState, FlowState, Settings, State},
};

static CAT: LazyLock<gst::DebugCategory> = LazyLock::new(|| {
    gst::DebugCategory::new(
        "mxlsrc",
        gst::DebugColorFlags::empty(),
        Some("Rust MXL Source"),
    )
});

pub(crate) fn get_flow_type_id<'a>(
    settings: &'a MutexGuard<'a, Settings>,
) -> Result<&'a String, gst::LoggableError> {
    settings
        .flow_id()
        .ok_or(gst::loggable_error!(CAT, "No flow id was found"))
}

/// Amount to subtract from a grain's absolute MXL timestamp to get its PTS in
/// the pipeline running-time base: `D + base_time`. `offset` is the pipeline's
/// shared `D` (see [`crate::clock::ClockOffsetExt`]); `base_time` is read live
/// so a flushing seek that re-bases the pipeline is followed without re-sampling.
pub(crate) fn pts_subtrahend(src: &MxlSrc, offset: u64) -> Result<u64, gst::FlowError> {
    let base_time = src.obj().base_time().ok_or(gst::FlowError::Error)?;
    Ok(offset.saturating_add(base_time.nseconds()))
}

pub(crate) fn get_mxl_flow_json(
    instance: &MxlInstance,
    flow_id: &str,
) -> Result<serde_json::Value, gst::LoggableError> {
    let flow_def = instance
        .get_flow_def(flow_id)
        .map_err(|e| gst::loggable_error!(CAT, "Failed to get flow definition: {}", e))?;
    let serde_json: serde_json::Value = serde_json::from_str(flow_def.as_str())
        .map_err(|e| gst::loggable_error!(CAT, "Invalid JSON: {}", e))?;
    Ok(serde_json)
}

pub(crate) fn set_json_caps(src: &MxlSrc, json: FlowDefDetails) -> Result<(), gst::LoggableError> {
    match json {
        FlowDefDetails::Video(video) => {
            let caps = gst::Caps::builder("video/x-raw")
                .field("format", "v210")
                .field("width", video.frame_width)
                .field("height", video.frame_height)
                .field(
                    "framerate",
                    gst::Fraction::new(video.grain_rate.numerator, video.grain_rate.denominator),
                )
                .field("interlace-mode", video.interlace_mode.as_str())
                .field("colorimetry", video.colorspace.to_lowercase())
                .build();

            src.obj()
                .set_caps(&caps)
                .map_err(|err| gst::loggable_error!(CAT, "Failed to set caps: {}", err))?;

            gst::info!(CAT, imp = src, "Negotiated caps: {}", caps);
            Ok(())
        }
        FlowDefDetails::Audio(audio) => {
            let caps = gst::Caps::builder("audio/x-raw")
                .field("format", "F32LE")
                .field("rate", audio.sample_rate.numerator)
                .field("channels", audio.channel_count)
                .field("layout", "interleaved")
                .field(
                    "channel-mask",
                    generate_channel_mask_from_channels(audio.channel_count as u32),
                )
                .build();
            src.obj()
                .set_caps(&caps)
                .map_err(|err| gst::loggable_error!(CAT, "Failed to set caps: {}", err))?;

            gst::info!(CAT, imp = src, "Negotiated caps: {}", caps);
            Ok(())
        }
        FlowDefDetails::Data(data) => {
            let caps = gst::Caps::builder("meta/x-st-2038")
                .field(
                    "framerate",
                    gst::Fraction::new(data.grain_rate.numerator, data.grain_rate.denominator),
                )
                .field("alignment", "frame")
                .build();

            src.obj()
                .set_caps(&caps)
                .map_err(|err| gst::loggable_error!(CAT, "Failed to set caps: {}", err))?;

            gst::info!(CAT, imp = src, "Negotiated caps: {}", caps);
            Ok(())
        }
    }
}

pub(crate) fn get_flow_def(
    src: &MxlSrc,
    serde_json: serde_json::Value,
) -> Result<FlowDefDetails, gst::LoggableError> {
    let format = serde_json
        .get("format")
        .and_then(|v| v.as_str())
        .unwrap_or("unknown");
    let json = match format {
        "urn:x-nmos:format:video" => {
            let flow: FlowDefVideo = serde_json::from_value(serde_json)
                .map_err(|e| gst::loggable_error!(CAT, "Invalid video flow JSON: {}", e))?;
            FlowDefDetails::Video(flow)
        }
        "urn:x-nmos:format:audio" => {
            let flow: FlowDefAudio = serde_json::from_value(serde_json)
                .map_err(|e| gst::loggable_error!(CAT, "Invalid audio flow JSON: {}", e))?;
            FlowDefDetails::Audio(flow)
        }
        "urn:x-nmos:format:data" => {
            let flow: FlowDefData = serde_json::from_value(serde_json)
                .map_err(|e| gst::loggable_error!(CAT, "Invalid data flow JSON: {}", e))?;
            FlowDefDetails::Data(flow)
        }
        _ => {
            gst::warning!(CAT, imp = src, "Unknown format '{}'", format);
            return Err(gst::loggable_error!(CAT, "Unknown format {}", format));
        }
    };
    Ok(json)
}
pub(crate) fn generate_channel_mask_from_channels(channels: u32) -> gst::Bitmask {
    let mask = if channels >= 64 {
        u64::MAX
    } else {
        (1u64 << channels) - 1
    };
    gst::Bitmask::new(mask)
}

/// Video, audio, or discrete data.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum FlowKind {
    Video,
    Audio,
    Data,
}

/// Blocks until `create_flow_reader` succeeds, sleeping briefly and retrying
/// while MXL returns `FlowNotFound`.
///
/// Before each attempt, checks `is_flushing` and returns an error if so, to
/// avoid blocking teardown via `unlock` if the flow has not yet been created.
///
/// `flow_id` is passed in by the caller (typically cloned under a short
/// `settings` lock in `init`) so this function never holds
/// `MutexGuard<Settings>` across `thread::sleep` in the `FlowNotFound` loop.
fn init_mxl_reader(
    mxlsrc: &MxlSrc,
    instance: &MxlInstance,
    flow_id: &str,
) -> Result<FlowReader, gst::ErrorMessage> {
    let mut warned = false;
    loop {
        if is_flushing(mxlsrc) {
            return Err(gst::error_msg!(
                gst::CoreError::Failed,
                ["Aborted waiting for flow"]
            ));
        }
        match instance.create_flow_reader(flow_id) {
            Ok(reader) => break Ok(reader),
            Err(mxl::Error::FlowNotFound) => {
                if !warned {
                    eprintln!("Waiting for flow to be created...");
                    warned = true;
                }
                std::thread::sleep(Duration::from_millis(50));
                continue;
            }
            Err(err) => {
                break Err(gst::error_msg!(
                    gst::CoreError::Failed,
                    ["Failed to create flow reader: {}", err]
                ));
            }
        }
    }
}

pub(crate) fn is_flushing(mxlsrc: &MxlSrc) -> bool {
    mxlsrc
        .clock_wait
        .lock()
        .map(|cw| cw.flushing)
        .unwrap_or(true)
}

/// The MXL instance, created on first use and cached in `Context` so the
/// reader and the timestamp conversions all share it. Idempotent and race-safe:
/// the first caller wins, later callers reuse the cached instance.
pub(crate) fn ensure_instance(mxlsrc: &MxlSrc) -> Result<MxlInstance, gst::ErrorMessage> {
    if let Some(instance) = mxlsrc
        .context
        .lock()
        .map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to get context mutex: {}", e]
            )
        })?
        .instance
        .clone()
    {
        return Ok(instance);
    }

    let domain = mxlsrc
        .settings
        .lock()
        .map_err(|_| gst::error_msg!(gst::CoreError::Failed, ["Missing settings"]))?
        .domain
        .clone();
    if domain.is_empty() {
        return Err(gst::error_msg!(gst::CoreError::Failed, ["domain not set"]));
    }
    let instance = init_mxl_instance(domain.as_str())?;

    let mut context = mxlsrc.context.lock().map_err(|e| {
        gst::error_msg!(
            gst::CoreError::Failed,
            ["Failed to get context mutex: {}", e]
        )
    })?;
    // Lost the race while we were creating ours: keep the winner's instance.
    if let Some(instance) = context.instance.clone() {
        return Ok(instance);
    }
    context.instance = Some(instance.clone());
    Ok(instance)
}

pub(crate) fn init(mxlsrc: &MxlSrc) -> Result<(), gst::ErrorMessage> {
    let (flow_kind, flow_id) = {
        let settings = mxlsrc
            .settings
            .lock()
            .map_err(|_| gst::error_msg!(gst::CoreError::Failed, ["Missing settings"]))?;
        if let Some(flow_id) = settings.video_flow.clone() {
            (FlowKind::Video, flow_id)
        } else if let Some(flow_id) = settings.audio_flow.clone() {
            (FlowKind::Audio, flow_id)
        } else if let Some(flow_id) = settings.data_flow.clone() {
            (FlowKind::Data, flow_id)
        } else {
            return Err(gst::error_msg!(
                gst::CoreError::Failed,
                ["Set exactly one of video-flow-id, audio-flow-id, or data-flow-id"]
            ));
        }
    };

    let instance = ensure_instance(mxlsrc)?;

    // Wait for the flow to be created without holding `settings` or `context` mutexes
    // across the poll/sleep loop.
    let reader = init_mxl_reader(mxlsrc, &instance, flow_id.as_str())?;
    let binding = reader.get_info();
    let reader_info = binding.as_ref();

    let mut context = mxlsrc.context.lock().map_err(|e| {
        gst::error_msg!(
            gst::CoreError::Failed,
            ["Failed to get context mutex: {}", e]
        )
    })?;

    match flow_kind {
        FlowKind::Video => {
            let grain_rate = reader_info
                .map_err(|e| {
                    gst::error_msg!(
                        gst::CoreError::Failed,
                        ["Failed to initialize MXL reader info: {}", e]
                    )
                })?
                .config
                .common()
                .grain_rate()
                .map_err(|e| {
                    gst::error_msg!(
                        gst::CoreError::Failed,
                        ["Failed to initialize MXL discrete flow info: {}", e]
                    )
                })?;
            let grain_reader = reader.to_grain_reader().map_err(|e| {
                gst::error_msg!(
                    gst::CoreError::Failed,
                    ["Failed to initialize MXL grain reader: {}", e]
                )
            })?;

            context.state = Some(State {
                instance,
                flow_state: Some(FlowState::Discrete(DiscreteState {
                    format: DiscreteFormat::Video,
                    grain_rate,
                    index: 0,
                    is_initialized: false,
                    next_discont: false,
                    grain_reader,
                })),
            });
        }
        FlowKind::Audio => {
            let reader_samples = init_mxl_reader(mxlsrc, &instance, flow_id.as_str())?;
            let samples_reader = reader_samples.to_samples_reader().map_err(|e| {
                gst::error_msg!(
                    gst::CoreError::Failed,
                    ["Failed to initialize MXL grain reader: {}", e]
                )
            })?;
            context.state = Some(State {
                instance,
                flow_state: Some(FlowState::Continuous(ContinuousState {
                    reader,
                    samples_reader,
                    is_initialized: false,
                    index: 0,
                    next_discont: false,
                })),
            });
        }
        FlowKind::Data => {
            let grain_rate = reader_info
                .map_err(|e| {
                    gst::error_msg!(
                        gst::CoreError::Failed,
                        ["Failed to initialize MXL reader info: {}", e]
                    )
                })?
                .config
                .common()
                .grain_rate()
                .map_err(|e| {
                    gst::error_msg!(
                        gst::CoreError::Failed,
                        ["Failed to initialize MXL discrete flow info: {}", e]
                    )
                })?;
            let grain_reader = reader.to_grain_reader().map_err(|e| {
                gst::error_msg!(
                    gst::CoreError::Failed,
                    ["Failed to initialize MXL grain reader: {}", e]
                )
            })?;

            context.state = Some(State {
                instance,
                flow_state: Some(FlowState::Discrete(DiscreteState {
                    format: DiscreteFormat::Data,
                    grain_rate,
                    index: 0,
                    is_initialized: false,
                    next_discont: false,
                    grain_reader,
                })),
            });
        }
    }
    Ok(())
}

fn init_mxl_instance(domain: &str) -> Result<MxlInstance, gst::ErrorMessage> {
    let mxl_api = mxl::load_api(get_mxl_so_path())
        .map_err(|e| gst::error_msg!(gst::CoreError::Failed, ["Failed to load MXL API: {}", e]))?;

    let mxl_instance = mxl::MxlInstance::new(mxl_api, domain, "").map_err(|e| {
        gst::error_msg!(
            gst::CoreError::Failed,
            ["Failed to load MXL instance: {}", e]
        )
    })?;

    // Best-effort: reclaim any flow directories left behind by a writer that
    // exited or crashed before its destructors ran. Long-running processes
    // get a fresh GC pass every time an element opens an instance.
    if let Err(e) = mxl_instance.garbage_collect_flows() {
        gst::warning!(CAT, "MXL garbage collection on init failed: {}", e);
    }

    Ok(mxl_instance)
}
