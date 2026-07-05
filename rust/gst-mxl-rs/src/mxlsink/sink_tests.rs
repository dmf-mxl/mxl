// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#[cfg(test)]
mod tests {
    use std::collections::HashMap;

    use gst::{CoreError, Fraction};
    use gstreamer as gst;
    use mxl::flowdef::*;
    use uuid::Uuid;

    #[test]
    #[cfg_attr(feature = "tracing", tracing_test::traced_test)]
    fn flow_def_generation() -> Result<(), glib::Error> {
        let flow_id = String::from("5fbec3b1-1b0f-417d-9059-8b94a47197ed");
        let width = 1920;
        let height = 1080;
        let framerate = Fraction::new(30000, 1001);
        let interlace_mode = InterlaceMode::Progressive;
        let colorimetry = "BT709".to_string();
        let format = "v210".to_string();
        let mut tags = HashMap::new();
        tags.insert(
            "urn:x-nmos:tag:grouphint/v1.0".to_string(),
            vec!["Media Function XYZ:Audio".to_string()],
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
            id: Uuid::parse_str(&flow_id)
                .map_err(|_| glib::Error::new(CoreError::Failed, "Failed to parse UUID"))?,
            description: format!(
                "MXL Test Flow, 1080p{}",
                framerate.numer() / framerate.denom()
            ),
            tags,
            format: "urn:x-nmos:format:video".into(),
            label: format!(
                "MXL Test Flow, 1080p{}",
                framerate.numer() / framerate.denom()
            ),
            parents: vec![],
            media_type: format!("video/{}", format),
            details: FlowDefDetails::Video(flow_def_details),
        };

        let json = serde_json::to_value(&flow_def)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Failed to convert to json"))?;

        let expected_json = serde_json::json!({
            "description": "MXL Test Flow, 1080p29",
            "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ed",
            "tags": {"urn:x-nmos:tag:grouphint/v1.0": ["Media Function XYZ:Audio"]},
            "format": "urn:x-nmos:format:video",
            "label": "MXL Test Flow, 1080p29",
            "parents": [],
            "media_type": "video/v210",
            "grain_rate": {
                "numerator": 30000,
                "denominator": 1001
            },
            "frame_width": 1920,
            "frame_height": 1080,
            "interlace_mode": "progressive",
            "colorspace": "BT709",
            "components": [
                {
                    "name": "Y",
                    "width": 1920,
                    "height": 1080,
                    "bit_depth": 10
                },
                {
                    "name": "Cb",
                    "width": 960,
                    "height": 1080,
                    "bit_depth": 10
                },
                {
                    "name": "Cr",
                    "width": 960,
                    "height": 1080,
                    "bit_depth": 10
                }
            ]
        });
        println!("{:#?}", json);
        assert_eq!(json, expected_json);
        Ok(())
    }
}
