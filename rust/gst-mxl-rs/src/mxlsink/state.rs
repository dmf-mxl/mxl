// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{collections::HashMap, process, str::FromStr};

use crate::mxlsink::imp::CAT;
use gst::StructureRef;
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

#[derive(Debug, Clone)]
pub(crate) struct Settings {
    pub flow_id: String,
    pub domain: String,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            flow_id: DEFAULT_FLOW_ID.to_owned(),
            domain: DEFAULT_DOMAIN.to_owned(),
        }
    }
}

pub(crate) struct State {
    pub instance: MxlInstance,
    pub flow: Option<FlowConfigInfo>,
    pub video: Option<VideoState>,
    pub audio: Option<AudioState>,
    pub data: Option<DataState>,
}

pub(crate) struct VideoState {
    pub writer: GrainWriter,
}

pub(crate) struct AudioState {
    pub writer: SamplesWriter,
    pub flow_def: FlowDefAudio,
}

pub(crate) struct DataState {
    pub writer: GrainWriter,
}

#[derive(Default)]
pub(crate) struct Context {
    pub state: Option<State>,
}

pub(crate) fn init_state_with_video(
    state: &mut State,
    structure: &StructureRef,
    flow_id: &str,
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
    let pid = process::id();
    let mut tags = HashMap::new();
    tags.insert(
        "urn:x-nmos:tag:grouphint/v1.0".to_string(),
        vec![format!("Media Function {}:Video", pid).to_string()],
    );
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
        id: Uuid::parse_str(flow_id)
            .map_err(|e| gst::loggable_error!(CAT, "Flow ID is invalid: {}", e))?,
        description: format!(
            "MXL Test Flow, {}p{}",
            height,
            framerate.numer() / framerate.denom()
        ),
        tags,
        format: "urn:x-nmos:format:video".into(),
        label: format!(
            "MXL Test Flow, {}p{}",
            height,
            framerate.numer() / framerate.denom()
        ),
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
    state.video = Some(VideoState { writer });
    state.flow = Some(flow);

    Ok(())
}

pub(crate) fn init_state_with_audio(
    state: &mut State,
    info: AudioInfo,
    flow_id: &str,
) -> Result<(), gst::LoggableError> {
    let channels = info.channels() as i32;
    let rate = info.rate() as i32;
    let bit_depth = info.depth() as u8;
    let format = info.format().to_string();
    let pid = process::id();
    let mut tags = HashMap::new();
    tags.insert(
        "urn:x-nmos:tag:grouphint/v1.0".to_string(),
        vec![format!("Media Function {}:Audio", pid).to_string()],
    );

    let flow_def_details = FlowDefAudio {
        sample_rate: Rate {
            numerator: rate,
            denominator: 1,
        },
        channel_count: channels,
        bit_depth,
    };

    let flow_def = FlowDef {
        id: Uuid::parse_str(flow_id)
            .map_err(|e| gst::loggable_error!(CAT, "Flow ID is invalid: {}", e))?,
        description: "MXL Audio Flow".into(),
        format: "urn:x-nmos:format:audio".into(),
        tags,
        label: "MXL Audio Flow".into(),
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
    state.audio = Some(AudioState {
        writer,
        flow_def: flow_def_details,
    });
    state.flow = Some(flow);

    trace!(
        "Made it to the end of set_caps with format {}, channel_count {}, sample_rate {}, bit_depth {}",
        format, channels, rate, bit_depth
    );
    Ok(())
}

pub(crate) fn init_state_with_data(
    state: &mut State,
    structure: &StructureRef,
    flow_id: &str,
) -> Result<(), gst::LoggableError> {
    let framerate = structure
        .get::<gst::Fraction>("framerate")
        .unwrap_or_else(|_| gst::Fraction::new(30000, 1001));
    let pid = process::id();
    let mut tags = HashMap::new();
    tags.insert(
        "urn:x-nmos:tag:grouphint/v1.0".to_string(),
        vec![format!("Media Function {}:Data", pid).to_string()],
    );
    let flow_def_details = FlowDefData {
        grain_rate: Rate {
            numerator: framerate.numer(),
            denominator: framerate.denom(),
        },
    };
    let flow_def = FlowDef {
        id: Uuid::parse_str(flow_id)
            .map_err(|e| gst::loggable_error!(CAT, "Flow ID is invalid: {}", e))?,
        description: "MXL SMPTE 291 data flow".into(),
        tags,
        format: "urn:x-nmos:format:data".into(),
        label: "MXL data flow".into(),
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
    state.data = Some(DataState { writer });
    state.flow = Some(flow);

    Ok(())
}
