// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{collections::HashMap, ops::Deref};

use crate::mxlsink::imp::CAT;
use gst::{ClockTime, StructureRef};
use gst_audio::AudioInfo;
use gstreamer as gst;
use gstreamer_audio as gst_audio;
use mxl::{
    FlowConfigInfo, GrainWriter, MxlInstance, Rational, SamplesWriter,
    flowdef::{Component, FlowDefAudio, FlowDefVideo, GrainRate, SampleRate},
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
    pub initial_time: Option<InitialTime>,
}

pub(crate) struct VideoState {
    pub writer: GrainWriter,
    pub grain_index: u64,
    pub grain_rate: Rational,
    pub grain_count: u32,
}

pub(crate) struct AudioState {
    pub writer: SamplesWriter,
    pub bit_depth: u8,
    pub batch_size: usize,
    pub flow_def: FlowDefAudio,
}

#[derive(Default)]
pub(crate) struct Context {
    pub state: Option<State>,
}

#[derive(Default, Debug, Clone)]
pub(crate) struct InitialTime {
    pub index: u64,
    pub mxl_to_gst_offset: ClockTime,
}

pub(crate) fn init_state_with_audio(
    state: &mut State,
    info: AudioInfo,
    flow_id: &String,
) -> Result<(), gst::LoggableError> {
    let channels = info.channels() as i32;
    let rate = info.rate() as i32;
    let bit_depth = info.depth() as u8;
    let format = info.format().to_string();
    let mut tags = HashMap::new();
    tags.insert(
        "urn:x-nmos:tag:grouphint/v1.0".to_string(),
        vec!["Media Function XYZ:Audio".to_string()],
    );
    let flow_def = FlowDefAudio {
        description: "MXL Audio Flow".into(),
        format: "urn:x-nmos:format:audio".into(),
        tags,
        label: "MXL Audio Flow".into(),
        id: Uuid::parse_str(flow_id)
            .map_err(|e| gst::loggable_error!(CAT, "Flow ID is invalid: {}", e))?,
        media_type: "audio/float32".to_string(),
        sample_rate: SampleRate { numerator: rate },
        channel_count: channels,
        bit_depth,
        parents: vec![],
    };

    let instance = &state.instance;
    let flow = instance
        .create_flow(
            serde_json::to_string(&flow_def)
                .map_err(|e| gst::loggable_error!(CAT, "Failed to convert: {}", e))?
                .as_str(),
            None,
        )
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create audio flow: {}", e))?;

    let writer = instance
        .create_flow_writer(flow_id.as_str())
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create flow writer: {}", e))?
        .to_samples_writer()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?;
    state.audio = Some(AudioState {
        writer,
        bit_depth,
        batch_size: flow.common().max_commit_batch_size_hint() as usize,
        flow_def,
    });
    state.flow = Some(flow);

    trace!(
        "Made it to the end of set_caps with format {}, channel_count {}, sample_rate {}, bit_depth {}",
        format, channels, rate, bit_depth
    );
    Ok(())
}

pub(crate) fn init_state_with_video(
    state: &mut State,
    structure: &StructureRef,
    flow_id: &String,
) -> Result<(), gst::LoggableError> {
    let format = structure
        .get::<String>("format")
        .unwrap_or_else(|_| "v210".to_string());
    let width = structure.get::<i32>("width").unwrap_or(1920);
    let height = structure.get::<i32>("height").unwrap_or(1080);
    let framerate = structure
        .get::<gst::Fraction>("framerate")
        .unwrap_or_else(|_| gst::Fraction::new(30000, 1001));
    let interlace_mode = structure
        .get::<String>("interlace-mode")
        .unwrap_or_else(|_| "progressive".to_string());
    let colorimetry = structure
        .get::<String>("colorimetry")
        .unwrap_or_else(|_| "BT709".to_string());
    let mut tags = HashMap::new();
    tags.insert(
        "urn:x-nmos:tag:grouphint/v1.0".to_string(),
        vec!["Media Function XYZ:Video".to_string()],
    );
    let flow_def = FlowDefVideo {
        description: format!(
            "MXL Test Flow, 1080p{}",
            framerate.numer() / framerate.denom()
        ),
        id: flow_id.deref().into(),
        tags,
        format: "urn:x-nmos:format:video".into(),
        label: format!(
            "MXL Test Flow, 1080p{}",
            framerate.numer() / framerate.denom()
        ),
        parents: vec![],
        media_type: format!("video/{}", format),
        grain_rate: GrainRate {
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
    let instance = &state.instance;
    let flow = instance
        .create_flow(
            serde_json::to_string(&flow_def)
                .map_err(|e| gst::loggable_error!(CAT, "Failed to convert: {}", e))?
                .as_str(),
            None,
        )
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create flow: {}", e))?;
    let grain_rate = flow
        .common()
        .grain_rate()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to get grain rate: {}", e))?;
    let grain_count = flow
        .discrete()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to get grain count: {}", e))?
        .grainCount;
    let writer = instance
        .create_flow_writer(flow_id.as_str())
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create flow writer: {}", e))?
        .to_grain_writer()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?;
    let rate = flow
        .common()
        .grain_rate()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to get grain rate: {}", e))?;
    let index = instance.get_current_index(&rate);
    state.video = Some(VideoState {
        writer,
        grain_index: index,
        grain_rate,
        grain_count,
    });
    state.flow = Some(flow);

    Ok(())
}
