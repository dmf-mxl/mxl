// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{
    sync::{LazyLock, MutexGuard},
    time::Duration,
};

use glib::subclass::types::ObjectSubclassExt;
use gst::ClockTime;
use gst_base::prelude::*;
use gstreamer as gst;
use gstreamer_base as gst_base;
use mxl::{FlowReader, MxlInstance, config::get_mxl_so_path, flowdef::*};

use crate::mxlsrc::{
    imp::*,
    state::{AudioState, DataState, InitialTime, Settings, State, VideoState},
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
                .field(
                    "interlace-mode",
                    serde_json::to_string(&video.interlace_mode).map_err(|err| {
                        gst::loggable_error!(CAT, "Invalid interlace-mode: {}", err)
                    })?,
                )
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

fn init_mxl_reader(settings: &MutexGuard<'_, Settings>) -> Result<FlowReader, gst::ErrorMessage> {
    let mxl_instance = init_mxl_instance(settings)?;
    let flow_id = settings
        .flow_id()
        .ok_or_else(|| gst::error_msg!(gst::CoreError::Failed, ["Missing flow id in settings"]))?;
    let mut warned = false;
    loop {
        match mxl_instance.create_flow_reader(flow_id) {
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

pub(crate) fn init(mxlsrc: &MxlSrc) -> Result<(), gst::ErrorMessage> {
    let settings = mxlsrc
        .settings
        .lock()
        .map_err(|_| gst::error_msg!(gst::CoreError::Failed, ["Missing settings"]))?;

    let mut context = mxlsrc.context.lock().map_err(|e| {
        gst::error_msg!(
            gst::CoreError::Failed,
            ["Failed to get context mutex: {}", e]
        )
    })?;

    let reader = init_mxl_reader(&settings)?;
    let binding = reader.get_info();
    let reader_info = binding.as_ref();
    let instance = init_mxl_instance(&settings).map_err(|e| {
        gst::error_msg!(
            gst::CoreError::Failed,
            ["Failed to initialize MXL instance: {}", e]
        )
    })?;

    let initial_info = InitialTime {
        mxl_index: 0,
        gst_time: ClockTime::from_mseconds(0),
    };
    if settings.video_flow.is_some() {
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
            initial_info,
            video: Some(VideoState {
                grain_rate,
                frame_counter: 0,
                is_initialized: false,
                grain_reader,
            }),
            audio: None,
            data: None,
        });
    } else if settings.audio_flow.is_some() {
        let reader_audio = init_mxl_reader(&settings)?;
        let samples_reader = reader_audio.to_samples_reader().map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to initialize MXL grain reader: {}", e]
            )
        })?;
        context.state = Some(State {
            instance,
            initial_info,
            video: None,
            audio: Some(AudioState {
                reader,
                samples_reader,
                batch_counter: 0,
                is_initialized: false,
                index: 0,
                next_discont: false,
            }),
            data: None,
        });
    } else if settings.data_flow.is_some() {
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
            initial_info,
            video: None,
            audio: None,
            data: Some(DataState {
                grain_rate,
                frame_counter: 0,
                is_initialized: false,
                grain_reader,
            }),
        });
    } else {
        return Err(gst::error_msg!(
            gst::CoreError::Failed,
            ["Set exactly one of video-flow-id, audio-flow-id, or data-flow-id"]
        ));
    }
    Ok(())
}

fn init_mxl_instance(
    settings: &MutexGuard<'_, Settings>,
) -> Result<MxlInstance, gst::ErrorMessage> {
    let mxl_api = mxl::load_api(get_mxl_so_path())
        .map_err(|e| gst::error_msg!(gst::CoreError::Failed, ["Failed to load MXL API: {}", e]))?;

    let mxl_instance =
        mxl::MxlInstance::new(mxl_api, settings.domain.as_str(), "").map_err(|e| {
            gst::error_msg!(
                gst::CoreError::Failed,
                ["Failed to load MXL instance: {}", e]
            )
        })?;

    Ok(mxl_instance)
}
