// SPDX-FileCopyrightText: 2025-2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use mxl::{FlowReader, GrainReader, MxlInstance, Rational, SamplesReader};

pub(crate) const DEFAULT_FLOW_ID: &str = "";
pub(crate) const DEFAULT_DOMAIN: &str = "";

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
    pub video: Option<VideoState>,
    pub audio: Option<AudioState>,
    pub data: Option<DataState>,
}

pub struct VideoState {
    pub grain_rate: Rational,
    /// Next absolute MXL grain index to read.
    pub index: u64,
    pub is_initialized: bool,
    pub next_discont: bool,
    pub grain_reader: GrainReader,
}

pub struct AudioState {
    pub reader: FlowReader,
    pub samples_reader: SamplesReader,
    pub is_initialized: bool,
    pub index: u64,
    pub next_discont: bool,
}

pub struct DataState {
    pub grain_rate: Rational,
    /// Next absolute MXL grain index to read.
    pub index: u64,
    pub is_initialized: bool,
    pub next_discont: bool,
    pub grain_reader: GrainReader,
}

#[derive(Default)]
pub struct Context {
    /// MXL instance, created in `start()` so the reader and the timestamp
    /// conversions can share it. Cheap to clone (`Arc`-backed).
    pub instance: Option<MxlInstance>,
    pub state: Option<State>,
}
