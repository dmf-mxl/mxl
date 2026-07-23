// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{collections::HashMap, process, str::FromStr};

use crate::mxlsink::imp::CAT;
use gst::StructureRef;
use gst::prelude::*;
use gst_audio::AudioInfo;
use gstreamer as gst;
use gstreamer_audio as gst_audio;
use mxl::{
    FlowConfigInfo, GrainWriter, MxlInstance, SamplesWriter,
    flowdef::{
        Component, FlowDef, FlowDefAudio, FlowDefData, FlowDefDetails, FlowDefVideo, InterlaceMode,
        Rate,
    },
};
use tracing::trace;

use uuid::Uuid;

pub(crate) const DEFAULT_FLOW_ID: &str = "";
pub(crate) const DEFAULT_DOMAIN: &str = "";
pub(crate) const GROUPHINT_TAG: &str = "urn:x-nmos:tag:grouphint/v1.0";

#[derive(Debug, Clone)]
pub(crate) struct Settings {
    /// UUID of the MXL flow.
    pub flow_id: String,
    /// Local path to the MXL domain directory.
    pub domain: String,
    /// Top-level flow_def `label`. Empty keeps the built-in default.
    pub label: String,
    /// Top-level flow_def `description`. Empty keeps the built-in default.
    pub description: String,
    /// `urn:x-nmos:tag:grouphint/v1.0` value. Empty keeps the built-in default.
    pub group_hint: String,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            flow_id: DEFAULT_FLOW_ID.to_owned(),
            domain: DEFAULT_DOMAIN.to_owned(),
            label: String::new(),
            description: String::new(),
            group_hint: String::new(),
        }
    }
}

/// Format a rational frame rate for human-readable flow metadata.
/// Exact integers stay unadorned (`25/1` → `"25"`); otherwise two decimals
/// (`60000/1001` → `"59.94"`, `24000/1001` → `"23.98"`).
pub(crate) fn format_framerate(numer: i32, denom: i32) -> String {
    if denom != 0 && numer % denom == 0 {
        (numer / denom).to_string()
    } else {
        format!("{:.2}", numer as f64 / denom as f64)
    }
}

/// Format a sample rate in Hz as a compact kHz string (`48000` → `"48 kHz"`,
/// `44100` → `"44.1 kHz"`).
pub(crate) fn format_sample_rate_khz(rate_hz: i32) -> String {
    if rate_hz % 1000 == 0 {
        format!("{} kHz", rate_hz / 1000)
    } else {
        format!("{:.1} kHz", rate_hz as f64 / 1000.0)
    }
}

/// Replace `:` so grouphint group/role components stay well-formed.
fn sanitize_grouphint_component(s: &str) -> String {
    s.replace(':', "-")
}

/// Walk parents for the enclosing [`gst::Pipeline`] name, if any.
fn containing_pipeline_name(element: &gst::Element) -> Option<String> {
    let mut current = element.upcast_ref::<gst::Object>().parent();
    while let Some(obj) = current {
        if let Ok(pipeline) = obj.clone().downcast::<gst::Pipeline>() {
            return Some(pipeline.name().to_string());
        }
        current = obj.parent();
    }
    None
}

/// Built-in group hint: process- and pipeline-scoped group name, plus a
/// media-type role that includes the GStreamer element name so multiple sinks
/// of the same type stay unique
/// (`Media Function 12345 pipeline0:Video mxlsink0`).
pub(crate) fn default_group_hint(media_role: &str, element: &gst::Element) -> String {
    let group = match containing_pipeline_name(element) {
        Some(pipeline_name) => {
            format!("Media Function {} {}", process::id(), pipeline_name)
        }
        None => format!("Media Function {}", process::id()),
    };
    let role = format!("{media_role} {}", element.name());
    format!(
        "{}:{}",
        sanitize_grouphint_component(&group),
        sanitize_grouphint_component(&role)
    )
}

/// Resolve optional `label` / `description` / `group-hint` property overrides
/// against the built-in defaults used when those properties are empty.
/// `default_name` is used for both `label` and `description` when unset.
pub(crate) fn resolve_flow_metadata(
    settings: &Settings,
    default_name: String,
    default_group_hint: String,
) -> (String, String, HashMap<String, Vec<String>>) {
    let label = if settings.label.is_empty() {
        default_name.clone()
    } else {
        settings.label.clone()
    };
    let description = if settings.description.is_empty() {
        default_name
    } else {
        settings.description.clone()
    };
    let group_hint = if settings.group_hint.is_empty() {
        default_group_hint
    } else {
        settings.group_hint.clone()
    };
    let mut tags = HashMap::new();
    tags.insert(GROUPHINT_TAG.to_string(), vec![group_hint]);
    (label, description, tags)
}

pub(crate) struct State {
    pub instance: MxlInstance,
    pub flow_config: Option<FlowConfigInfo>,
    /// Writer state after `set_caps`; `None` between `start` and caps.
    pub flow_state: Option<FlowState>,
}

/// Mutually exclusive writer kinds for a single MXL flow.
pub(crate) enum FlowState {
    Discrete(DiscreteState),
    Continuous(ContinuousState),
}

#[derive(Clone, Copy)]
pub(crate) enum DiscreteFormat {
    Video,
    Data,
}

pub(crate) struct DiscreteState {
    pub format: DiscreteFormat,
    pub writer: GrainWriter,
}

pub(crate) struct ContinuousState {
    pub writer: SamplesWriter,
    pub flow_def: FlowDefAudio,
}

#[derive(Default)]
pub(crate) struct Context {
    pub state: Option<State>,
}

pub(crate) fn init_state_with_video(
    state: &mut State,
    structure: &StructureRef,
    settings: &Settings,
    element: &gst::Element,
) -> Result<(), gst::LoggableError> {
    let format = structure
        .get::<String>("format")
        .unwrap_or_else(|_| "v210".to_string());
    let width = structure.get::<i32>("width").unwrap_or(1920);
    let height = structure.get::<i32>("height").unwrap_or(1080);
    let framerate = structure
        .get::<gst::Fraction>("framerate")
        .unwrap_or_else(|_| gst::Fraction::new(30000, 1001));
    let interlace = structure
        .get::<String>("interlace-mode")
        .unwrap_or_else(|_| "progressive".to_string());
    let interlace_mode =
        InterlaceMode::from_str(interlace.as_str()).unwrap_or(InterlaceMode::Progressive);
    let colorimetry = structure
        .get::<String>("colorimetry")
        .unwrap_or_else(|_| "BT709".to_string());
    let default_name = format!(
        "MXL Video Flow, {}p{}",
        height,
        format_framerate(framerate.numer(), framerate.denom())
    );
    let (label, description, tags) =
        resolve_flow_metadata(settings, default_name, default_group_hint("Video", element));
    let flow_def_details = FlowDefVideo {
        grain_rate: Rate {
            numerator: framerate.numer(),
            denominator: framerate.denom(),
        },
        frame_width: width,
        frame_height: height,
        interlace_mode,
        colorspace: colorimetry,
        components: vec![
            Component {
                name: "Y".into(),
                width,
                height,
                bit_depth: 10,
            },
            Component {
                name: "Cb".into(),
                width: width / 2,
                height,
                bit_depth: 10,
            },
            Component {
                name: "Cr".into(),
                width: width / 2,
                height,
                bit_depth: 10,
            },
        ],
    };
    let flow_def = FlowDef {
        id: Uuid::parse_str(&settings.flow_id)
            .map_err(|e| gst::loggable_error!(CAT, "Flow ID is invalid: {}", e))?,
        description,
        tags,
        format: "urn:x-nmos:format:video".into(),
        label,
        parents: vec![],
        media_type: format!("video/{}", format),
        details: mxl::flowdef::FlowDefDetails::Video(flow_def_details),
    };
    let instance = &state.instance;

    let (flow_writer, flow, is_created) = instance
        .create_flow_writer(
            serde_json::to_string(&flow_def)
                .map_err(|e| gst::loggable_error!(CAT, "Failed to convert: {}", e))?
                .as_str(),
            None,
        )
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create flow writer: {}", e))?;
    if !is_created {
        return Err(gst::loggable_error!(
            CAT,
            "The writer could not be created, the UUID belongs to a flow with another active writer"
        ));
    }
    let writer = flow_writer
        .to_grain_writer()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?;
    state.flow_state = Some(FlowState::Discrete(DiscreteState {
        format: DiscreteFormat::Video,
        writer,
    }));
    state.flow_config = Some(flow);

    Ok(())
}

pub(crate) fn init_state_with_audio(
    state: &mut State,
    info: AudioInfo,
    settings: &Settings,
    element: &gst::Element,
) -> Result<(), gst::LoggableError> {
    let channels = info.channels() as i32;
    let rate = info.rate() as i32;
    let bit_depth = info.depth() as u8;
    let format = info.format().to_string();
    let default_name = format!(
        "MXL Audio Flow, {} ch, {}",
        channels,
        format_sample_rate_khz(rate)
    );
    let (label, description, tags) =
        resolve_flow_metadata(settings, default_name, default_group_hint("Audio", element));

    let flow_def_details = FlowDefAudio {
        sample_rate: Rate {
            numerator: rate,
            denominator: 1,
        },
        channel_count: channels,
        bit_depth,
    };

    let flow_def = FlowDef {
        id: Uuid::parse_str(&settings.flow_id)
            .map_err(|e| gst::loggable_error!(CAT, "Flow ID is invalid: {}", e))?,
        description,
        format: "urn:x-nmos:format:audio".into(),
        tags,
        label,
        media_type: "audio/float32".to_string(),
        parents: vec![],
        details: FlowDefDetails::Audio(flow_def_details.clone()),
    };

    let (flow_writer, flow, is_created) = state
        .instance
        .create_flow_writer(
            serde_json::to_string(&flow_def)
                .map_err(|e| gst::loggable_error!(CAT, "Failed to convert: {}", e))?
                .as_str(),
            None,
        )
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create flow writer: {}", e))?;
    if !is_created {
        return Err(gst::loggable_error!(
            CAT,
            "The writer could not be created, the UUID belongs to a flow with another active writer"
        ));
    }
    let writer = flow_writer
        .to_samples_writer()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?;
    state.flow_state = Some(FlowState::Continuous(ContinuousState {
        writer,
        flow_def: flow_def_details,
    }));
    state.flow_config = Some(flow);

    trace!(
        "Made it to the end of set_caps with format {}, channel_count {}, sample_rate {}, bit_depth {}",
        format, channels, rate, bit_depth
    );
    Ok(())
}

pub(crate) fn init_state_with_data(
    state: &mut State,
    structure: &StructureRef,
    settings: &Settings,
    element: &gst::Element,
) -> Result<(), gst::LoggableError> {
    let framerate = structure
        .get::<gst::Fraction>("framerate")
        .unwrap_or_else(|_| gst::Fraction::new(30000, 1001));
    let default_name = format!(
        "MXL Data Flow, {} Hz",
        format_framerate(framerate.numer(), framerate.denom())
    );
    let (label, description, tags) =
        resolve_flow_metadata(settings, default_name, default_group_hint("Data", element));
    let flow_def_details = FlowDefData {
        grain_rate: Rate {
            numerator: framerate.numer(),
            denominator: framerate.denom(),
        },
    };
    let flow_def = FlowDef {
        id: Uuid::parse_str(&settings.flow_id)
            .map_err(|e| gst::loggable_error!(CAT, "Flow ID is invalid: {}", e))?,
        description,
        tags,
        format: "urn:x-nmos:format:data".into(),
        label,
        parents: vec![],
        media_type: "video/smpte291".into(),
        details: FlowDefDetails::Data(flow_def_details),
    };
    let instance = &state.instance;

    let (flow_writer, flow, is_created) = instance
        .create_flow_writer(
            serde_json::to_string(&flow_def)
                .map_err(|e| gst::loggable_error!(CAT, "Failed to convert: {}", e))?
                .as_str(),
            None,
        )
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create flow writer: {}", e))?;
    if !is_created {
        return Err(gst::loggable_error!(
            CAT,
            "The writer could not be created, the UUID belongs to a flow with another active writer"
        ));
    }
    let writer = flow_writer
        .to_grain_writer()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?;
    state.flow_state = Some(FlowState::Discrete(DiscreteState {
        format: DiscreteFormat::Data,
        writer,
    }));
    state.flow_config = Some(flow);

    Ok(())
}
