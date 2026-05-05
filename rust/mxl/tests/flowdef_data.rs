// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

use mxl::flowdef::{FlowDef, FlowDefData, FlowDefDetails, Rate};
use std::collections::HashMap;
use uuid::Uuid;

#[test]
fn data_flow_json_maps_to_flow_def_details_data() {
    let raw = include_str!("../../../lib/tests/data/data_flow.json");
    let v: serde_json::Value = serde_json::from_str(raw).expect("data_flow.json parses as JSON");
    assert_eq!(
        v["format"].as_str(),
        Some("urn:x-nmos:format:data"),
        "fixture format"
    );
    let details: FlowDefDetails =
        serde_json::from_value(v).expect("data_flow.json deserializes into FlowDefDetails::Data");
    assert_eq!(
        details,
        FlowDefDetails::Data(FlowDefData {
            grain_rate: Rate {
                numerator: 30000,
                denominator: 1001,
            },
        })
    );
}

#[test]
fn flow_def_data_details_roundtrip_serde() {
    let details = FlowDefDetails::Data(FlowDefData {
        grain_rate: Rate {
            numerator: 50,
            denominator: 1,
        },
    });
    let json = serde_json::to_string(&details).expect("serialize FlowDefDetails::Data");
    let parsed: FlowDefDetails =
        serde_json::from_str(&json).expect("deserialize FlowDefDetails::Data");
    assert_eq!(details, parsed);
}

/// Full [`FlowDef`] is not serde round-tripped here: `format` on the struct and
/// `#[serde(tag = "format")]` on flattened [`FlowDefDetails`] duplicate the JSON
/// key on serialize (same for video and audio). MXL code serializes to JSON for
/// the C API; use this test only to lock data-flow JSON shape.
#[test]
fn flow_def_data_serializes_like_mxlsink() {
    let flow = FlowDef {
        id: Uuid::new_v4(),
        description: "Long description of the data flow".into(),
        tags: HashMap::from([(
            "urn:x-nmos:tag:grouphint/v1.0".to_string(),
            vec!["test:Ancillary Data".to_string()],
        )]),
        format: "urn:x-nmos:format:data".into(),
        label: "Short description of the data flow".into(),
        parents: vec![],
        media_type: "video/smpte291".into(),
        details: FlowDefDetails::Data(FlowDefData {
            grain_rate: Rate {
                numerator: 50,
                denominator: 1,
            },
        }),
    };
    let json = serde_json::to_string(&flow).expect("serialize FlowDef data");
    let v: serde_json::Value = serde_json::from_str(&json).expect("parse as JSON Value");
    assert_eq!(
        v["format"].as_str(),
        Some("urn:x-nmos:format:data"),
        "NMOS format tag"
    );
    assert_eq!(v["media_type"].as_str(), Some("video/smpte291"));
    assert_eq!(v["grain_rate"]["numerator"], 50);
}
