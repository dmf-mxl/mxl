// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::collections::HashMap;

use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct GrainRate {
    pub numerator: i32,
    pub denominator: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Component {
    pub name: String,
    pub width: i32,
    pub height: i32,
    pub bit_depth: u8,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct FlowDefVideo {
    pub description: String,
    pub id: String,
    pub tags: HashMap<String, Vec<String>>,
    pub format: String,
    pub label: String,
    pub parents: Vec<String>,
    pub media_type: String,
    pub grain_rate: GrainRate,
    pub frame_width: i32,
    pub frame_height: i32,
    pub interlace_mode: String,
    pub colorspace: String,
    pub components: Vec<Component>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct SampleRate {
    pub numerator: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct FlowDefAudio {
    pub description: String,
    pub format: String,
    pub tags: HashMap<String, Vec<String>>,
    pub label: String,
    pub id: String,
    pub media_type: String,
    pub sample_rate: SampleRate,
    pub channel_count: i32,
    pub bit_depth: u8,
    pub parents: Vec<String>,
}

pub struct FlowDef {
    pub video: Option<FlowDefVideo>,
    pub audio: Option<FlowDefAudio>,
}
