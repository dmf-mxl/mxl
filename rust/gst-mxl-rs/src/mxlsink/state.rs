// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{
    collections::HashMap,
    process,
    str::FromStr,
    sync::{Arc, Condvar, Mutex},
    thread,
};

use crate::mxlsink::{
    imp::CAT,
    render_audio::{WriteGrainData, WriteSampleData, await_audio_buffer},
    render_data::await_data_buffer,
    render_video::await_video_buffer,
};
use crossbeam::channel::bounded;
use gst::StructureRef;
use gst_audio::AudioInfo;
use gstreamer as gst;
use gstreamer_audio as gst_audio;
use mxl::{
    FlowConfigInfo, GrainWriter, MxlInstance, Rational, SamplesWriter,
    flowdef::{
        Component, FlowDef, FlowDefAudio, FlowDefData, FlowDefDetails, FlowDefVideo, InterlaceMode,
        Rate,
    },
};
use tracing::trace;

use uuid::Uuid;

pub(crate) const DEFAULT_FLOW_ID: &str = "";
pub(crate) const DEFAULT_DOMAIN: &str = "";
const MAX_CHANNEL_SIZE: usize = 50;

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
    pub pipeline: gst::Pipeline,
}

pub(crate) struct VideoState {
    pub tx: crossbeam::channel::Sender<VideoCommand>,
    pub grain_index: u64,
    pub initial_time: Option<InitialTime>,
    pub latency: u64,
    pub sleep_flag: Arc<(Mutex<bool>, Condvar)>,
}

#[derive(Debug)]
pub(crate) struct AudioState {
    pub tx: crossbeam::channel::Sender<AudioCommand>,
    pub flow_def: FlowDefAudio,
    pub initial_time: Option<InitialTime>,
    pub latency: u64,
    pub sleep_flag: Arc<(Mutex<bool>, Condvar)>,
}

pub(crate) struct DataState {
    pub tx: crossbeam::channel::Sender<DataCommand>,
    pub grain_index: u64,
    pub initial_time: Option<InitialTime>,
    pub latency: u64,
    pub sleep_flag: Arc<(Mutex<bool>, Condvar)>,
}

pub enum VideoCommand {
    Write { data: WriteGrainData },
}

pub enum AudioCommand {
    Write { data: WriteSampleData },
}

pub enum DataCommand {
    Write { data: WriteGrainData },
}

#[derive(Default)]
pub(crate) struct Context {
    pub state: Option<State>,
}

pub struct VideoEngine {
    pub writer: Option<GrainWriter>,
    pub instance: MxlInstance,
    pub grain_rate: Rational,
    pub sleep_flag: Arc<(Mutex<bool>, Condvar)>,
}

pub struct AudioEngine {
    pub writer: Option<SamplesWriter>,
    pub instance: MxlInstance,
    pub sample_rate: Rational,
    pub sleep_flag: Arc<(Mutex<bool>, Condvar)>,
}

pub struct DataEngine {
    pub writer: Option<GrainWriter>,
    pub instance: MxlInstance,
    pub grain_rate: Rational,
    pub sleep_flag: Arc<(Mutex<bool>, Condvar)>,
}

#[derive(Debug, Clone)]
pub struct InitialTime {
    pub mxl_pts_offset: u64,
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
    let writer = Some(
        flow_writer
            .to_grain_writer()
            .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?,
    );
    let grain_rate = flow
        .common()
        .grain_rate()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to get grain rate: {}", e))?;
    let index = instance.get_current_index(&grain_rate);
    let instance = instance.clone();
    let (tx, rx) = bounded::<VideoCommand>(MAX_CHANNEL_SIZE);
    let sleep_flag_init = Arc::new((Mutex::new(false), Condvar::new()));
    let sleep_flag = sleep_flag_init.clone();
    thread::spawn(move || {
        let mut engine = VideoEngine {
            writer,
            instance,
            grain_rate,
            sleep_flag,
        };
        await_video_buffer(&mut engine, rx)
    });
    let sleep_flag = sleep_flag_init.clone();
    state.video = Some(VideoState {
        grain_index: index,
        initial_time: None,
        latency: 0,
        tx,
        sleep_flag,
    });
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
    let writer = Some(
        flow_writer
            .to_samples_writer()
            .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?,
    );
    let (tx, rx) = bounded::<AudioCommand>(MAX_CHANNEL_SIZE);
    let instance = state.instance.clone();
    if !is_created {
        return Err(gst::loggable_error!(
            CAT,
            "The writer could not be created, the UUID belongs to a flow with another active writer"
        ));
    }
    let sleep_flag_init = Arc::new((Mutex::new(false), Condvar::new()));
    let sleep_flag = sleep_flag_init.clone();
    let audio_state = AudioState {
        tx,
        flow_def: flow_def_details,
        initial_time: None,
        latency: 0,
        sleep_flag,
    };
    state.audio = Some(audio_state);
    state.flow = Some(flow);

    let sample_rate = Rational {
        numerator: rate as i64,
        denominator: 1,
    };

    let sleep_flag = sleep_flag_init.clone();
    thread::spawn(move || {
        let mut engine = AudioEngine {
            writer,
            instance,
            sample_rate,
            sleep_flag,
        };
        await_audio_buffer(&mut engine, rx)
    });
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
    let writer = Some(
        flow_writer
            .to_grain_writer()
            .map_err(|e| gst::loggable_error!(CAT, "Failed to create grain writer: {}", e))?,
    );
    let grain_rate = flow
        .common()
        .grain_rate()
        .map_err(|e| gst::loggable_error!(CAT, "Failed to get grain rate: {}", e))?;
    let index = instance.get_current_index(&grain_rate);
    let instance = instance.clone();
    let (tx, rx) = bounded::<DataCommand>(MAX_CHANNEL_SIZE);
    let sleep_flag_init = Arc::new((Mutex::new(false), Condvar::new()));
    let sleep_flag = sleep_flag_init.clone();
    thread::spawn(move || {
        let mut engine = DataEngine {
            writer,
            instance,
            grain_rate,
            sleep_flag,
        };
        await_data_buffer(&mut engine, rx)
    });
    let sleep_flag = sleep_flag_init.clone();
    state.data = Some(DataState {
        grain_index: index,
        initial_time: None,
        latency: 0,
        tx,
        sleep_flag,
    });
    state.flow = Some(flow);

    Ok(())
}
