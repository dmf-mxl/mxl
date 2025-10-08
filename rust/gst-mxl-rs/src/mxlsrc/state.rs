// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use gstreamer as gst;

use mxl::{FlowReader, GrainReader, MxlInstance, Rational, SamplesReader};

pub(crate) const DEFAULT_FLOW_ID: &str = "";
pub(crate) const DEFAULT_DOMAIN: &str = "";

#[derive(Debug, Default, Clone)]
pub struct InitialTime {
    pub mxl_index: u64,
    pub gst_time: gst::ClockTime,
}

#[derive(Debug, Clone)]
pub struct Settings {
    pub video_flow: Option<String>,
    pub audio_flow: Option<String>,
    pub domain: String,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            video_flow: None,
            audio_flow: None,
            domain: DEFAULT_DOMAIN.to_owned(),
        }
    }
}

pub struct State {
    pub instance: MxlInstance,
    pub initial_info: InitialTime,
    pub video: Option<VideoState>,
    pub audio: Option<AudioState>,
}

pub struct VideoState {
    pub grain_rate: Rational,
    pub frame_counter: u64,
    pub is_initialized: bool,
    pub grain_reader: GrainReader,
}

pub struct AudioState {
    pub reader: FlowReader,
    pub samples_reader: SamplesReader,
    pub batch_counter: u64,
    pub is_initialized: bool,
    pub index: u64,
    pub next_discont: bool,
}

#[derive(Default)]
pub struct Context {
    pub state: Option<State>,
}
