// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! JSON flow definition schema types.
//!
//! This module defines Rust structures for parsing and serializing MXL flow
//! definitions, which follow the NMOS IS-04 flow schema format.

use std::{collections::HashMap, str::FromStr};

use serde::{Deserialize, Serialize};

/// Complete flow definition structure following NMOS IS-04 schema.
///
/// This represents the JSON flow definition passed to
/// [`crate::MxlInstance::create_flow_writer`]. It contains both common metadata
/// (ID, labels, tags) and format-specific details (video dimensions, audio rates).
///
/// # Examples
///
/// ```no_run
/// use mxl::flowdef::FlowDef;
///
/// let json = r#"{
///     "id": "12345678-1234-1234-1234-123456789abc",
///     "format": "urn:x-nmos:format:video",
///     "label": "My Video Flow",
///     "description": "1080p60 video",
///     "media_type": "video/raw",
///     "grain_rate": {"numerator": 60, "denominator": 1},
///     "frame_width": 1920,
///     "frame_height": 1080,
///     "interlace_mode": "progressive",
///     "colorspace": "BT709",
///     "components": []
/// }"#;
///
/// let flow_def: FlowDef = serde_json::from_str(json).unwrap();
/// ```
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct FlowDef {
    /// Unique identifier for this flow (UUID).
    pub id: uuid::Uuid,
    /// Human-readable description.
    pub description: String,
    /// Arbitrary key-value tags for organization.
    pub tags: HashMap<String, Vec<String>>,
    /// NMOS format URN (e.g., "urn:x-nmos:format:video").
    pub format: String,
    /// Short human-readable label.
    pub label: String,
    /// List of parent flow IDs (for derived flows).
    pub parents: Vec<String>,
    /// MIME media type (e.g., "video/raw").
    pub media_type: String,
    /// Format-specific details (flattened into this struct via serde).
    #[serde(flatten)]
    pub details: FlowDefDetails,
}

/// Format-specific flow definition details.
///
/// This enum is used to deserialize the format-specific fields based on the
/// `format` field in the JSON.
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(tag = "format")]
pub enum FlowDefDetails {
    /// Video flow definition.
    #[serde(rename = "urn:x-nmos:format:video")]
    Video(FlowDefVideo),
    // TODO: Add support for "video/v210a" (video with alpha channel).
    /// Audio flow definition.
    #[serde(rename = "urn:x-nmos:format:audio")]
    Audio(FlowDefAudio),
}

/// Video flow definition details.
///
/// Specifies video-specific parameters like dimensions, frame rate, and color format.
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct FlowDefVideo {
    /// Frame rate as a rational number (e.g., 30000/1001 for 29.97 fps).
    pub grain_rate: Rate,
    /// Frame width in pixels.
    pub frame_width: i32,
    /// Frame height in pixels (or field height for interlaced).
    pub frame_height: i32,
    /// Interlacing mode.
    pub interlace_mode: InterlaceMode,
    /// Colorspace identifier (e.g., "BT709", "BT2020").
    pub colorspace: String,
    /// Video component descriptions (Y, Cb, Cr, etc.).
    pub components: Vec<Component>,
}

/// Video interlacing mode.
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub enum InterlaceMode {
    /// Progressive scan (non-interlaced).
    #[serde(rename = "progressive")]
    Progressive,
    /// Interlaced with top field first.
    #[serde(rename = "interlaced_tff")]
    InterlacedTff,
    /// Interlaced with bottom field first.
    #[serde(rename = "interlaced_bff")]
    InterlacedBff,
}

impl FromStr for InterlaceMode {
    type Err = ();

    /// Parses an interlace mode string.
    ///
    /// Accepts: "progressive", "interlaced_tff", or "interlaced_bff".
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "progressive" => Ok(Self::Progressive),
            "interlaced_tff" => Ok(Self::InterlacedTff),
            "interlaced_bff" => Ok(Self::InterlacedBff),
            _ => Err(()),
        }
    }
}

/// Audio flow definition details.
///
/// Specifies audio-specific parameters like sample rate and channel count.
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct FlowDefAudio {
    /// Sample rate as a rational number (typically 48000/1 Hz).
    pub sample_rate: Rate,
    /// Number of audio channels.
    pub channel_count: i32,
    /// Bit depth per sample (e.g., 24 for 24-bit float).
    pub bit_depth: u8,
}

/// Rational number representation for rates.
///
/// Used for frame rates (e.g., 30000/1001 for 29.97 fps) and sample rates
/// (e.g., 48000/1 for 48 kHz audio).
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Rate {
    /// Numerator of the rate.
    pub numerator: i32,
    /// Denominator of the rate (defaults to 1 if omitted in JSON).
    #[serde(default = "default_denominator")]
    pub denominator: i32,
}

/// Default denominator for rates (1 Hz).
fn default_denominator() -> i32 {
    1
}

/// Video component description (Y, Cb, Cr, alpha, etc.).
///
/// Describes a single component plane in a video frame, including its
/// dimensions and bit depth.
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Component {
    /// Component name (e.g., "Y", "Cb", "Cr", "A").
    pub name: String,
    /// Component width in pixels.
    pub width: i32,
    /// Component height in pixels.
    pub height: i32,
    /// Bit depth of this component.
    pub bit_depth: u8,
}
