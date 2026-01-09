// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use std::{collections::HashMap, str::FromStr};

use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct FlowDef {
    pub id: uuid::Uuid,
    pub description: String,
    pub tags: HashMap<String, Vec<String>>,
    pub format: String,
    pub label: String,
    pub parents: Vec<String>,
    pub media_type: String,
    #[serde(flatten)]
    pub details: FlowDefDetails,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(tag = "media_type")]
pub enum FlowDefDetails {
    #[serde(rename = "video/v210")]
    Video(FlowDefVideo),
    // TODO: Add support for "video/v210a".
    #[serde(rename = "audio/float32")]
    Audio(FlowDefAudio),
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct FlowDefVideo {
    pub grain_rate: Rate,
    pub frame_width: i32,
    pub frame_height: i32,
    pub interlace_mode: InterlaceMode,
    pub colorspace: String,
    pub components: Vec<Component>,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub enum InterlaceMode {
    #[serde(rename = "progressive")]
    Progressive,
    #[serde(rename = "interlaced_tff")]
    InterlacedTff,
    #[serde(rename = "interlaced_bff")]
    InterlacedBff,
}

impl FromStr for InterlaceMode {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "progressive" => Ok(Self::Progressive),
            "interlaced_tff" => Ok(Self::InterlacedTff),
            "interlaced_bff" => Ok(Self::InterlacedBff),
            _ => Err(()),
        }
    }
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct FlowDefAudio {
    pub sample_rate: Rate,
    pub channel_count: i32,
    pub bit_depth: u8,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Rate {
    pub numerator: i32,
    #[serde(default = "default_denominator")]
    pub denominator: i32,
}

fn default_denominator() -> i32 {
    1
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Component {
    pub name: String,
    pub width: i32,
    pub height: i32,
    pub bit_depth: u8,
}
