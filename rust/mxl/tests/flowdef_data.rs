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

#[test]
fn flow_def_data_roundrip() {
    let flow = FlowDef {
        id: Uuid::new_v4(),
        description: "Long description of the data flow".into(),
        tags: HashMap::from([(
            "urn:x-nmos:tag:grouphint/v1.0".to_string(),
            vec!["test:Ancillary Data".to_string()],
        )]),
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
    let def = serde_json::from_str::<FlowDef>(&json).expect("parse as JSON Value");
    assert_eq!(def, flow);
}
