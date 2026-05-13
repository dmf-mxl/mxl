// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
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
    pub data_flow: Option<String>,
    pub domain: String,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            video_flow: None,
            audio_flow: None,
            data_flow: None,
            domain: DEFAULT_DOMAIN.to_owned(),
        }
    }
}

impl Settings {
    /// How many of `video_flow` / `audio_flow` / `data_flow` hold a flow id (0–3).
    pub(crate) fn flow_id_count(&self) -> u8 {
        self.video_flow.is_some() as u8
            + self.audio_flow.is_some() as u8
            + self.data_flow.is_some() as u8
    }

    /// Flow id string from whichever slot is set (video, then audio, then data).
    pub(crate) fn flow_id(&self) -> Option<&String> {
        self.video_flow
            .as_ref()
            .or(self.audio_flow.as_ref())
            .or(self.data_flow.as_ref())
    }
}

pub struct State {
    pub instance: MxlInstance,
    pub initial_info: InitialTime,
    pub video: Option<VideoState>,
    pub audio: Option<AudioState>,
    pub data: Option<DataState>,
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

pub struct DataState {
    pub grain_rate: Rational,
    pub frame_counter: u64,
    pub is_initialized: bool,
    pub grain_reader: GrainReader,
}

#[derive(Default)]
pub struct Context {
    pub state: Option<State>,
}
