// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use mxl::flowdef::{Component, FlowDef, FlowDefAudio, FlowDefDetails, FlowDefVideo, Rate};
use std::collections::HashMap;
use uuid::Uuid;

#[test]
fn flow_def_roundtrip_video() {
    let flow = FlowDef {
        id: Uuid::new_v4(),
        description: "Long description of the video flow".into(),
        tags: HashMap::from([(
            "urn:x-nmos:tag:grouphint/v1.0".to_string(),
            vec!["test:My Video".to_string()],
        )]),
        label: "Video Flow".into(),
        parents: vec![],
        media_type: "video/v210".into(),
        details: FlowDefDetails::Video(FlowDefVideo {
            grain_rate: Rate {
                numerator: 50,
                denominator: 1,
            },
            frame_width: 1920,
            frame_height: 1080,
            interlace_mode: mxl::flowdef::InterlaceMode::Progressive,
            colorspace: "BT709".into(),
            components: vec![
                Component {
                    name: "Y".into(),
                    bit_depth: 10,
                    height: 1080,
                    width: 1920,
                },
                Component {
                    name: "Cb".into(),
                    bit_depth: 10,
                    height: 1080,
                    width: 1920 / 2,
                },
                Component {
                    name: "Cr".into(),
                    bit_depth: 10,
                    height: 1080,
                    width: 1920 / 2,
                },
            ],
        }),
    };

    let json = serde_json::to_string(&flow).expect("serialize FlowDef data");
    let parsed: FlowDef = serde_json::from_str(&json).expect("deserialize FlowDef");
    assert_eq!(parsed, flow);
}

#[test]
fn flow_def_roundtrip_audio() {
    let audio = FlowDef {
        id: Uuid::new_v4(),
        description: "Long description of the audio flow".into(),
        tags: HashMap::from([(
            "urn:x-nmos:tag:grouphint/v1.0".to_string(),
            vec!["test:My Audio".to_string()],
        )]),
        label: "Audio Flow".into(),
        parents: vec![],
        media_type: "audio/L24".into(),
        details: FlowDefDetails::Audio(FlowDefAudio {
            sample_rate: Rate {
                numerator: 48000,
                denominator: 1,
            },
            channel_count: 2,
            bit_depth: 24,
        }),
    };

    let json = serde_json::to_string(&audio).expect("serialize FlowDef data");
    let parsed: FlowDef = serde_json::from_str(&json).expect("deserialize FlowDef");
    assert_eq!(parsed, audio);
}
