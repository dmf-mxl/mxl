// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#[cfg(test)]
mod tests {
    use std::collections::HashMap;

    use crate::mxlsink::state::{
        GROUPHINT_TAG, Settings, default_group_hint, format_framerate, format_sample_rate_khz,
        resolve_flow_metadata,
    };
    use gst::prelude::*;
    use gst::{CoreError, Fraction};
    use gstreamer as gst;
    use mxl::flowdef::*;
    use uuid::Uuid;

    #[test]
    fn format_framerate_examples() {
        assert_eq!(format_framerate(25, 1), "25");
        assert_eq!(format_framerate(50, 1), "50");
        assert_eq!(format_framerate(30, 1), "30");
        assert_eq!(format_framerate(60000, 1001), "59.94");
        assert_eq!(format_framerate(30000, 1001), "29.97");
        assert_eq!(format_framerate(24000, 1001), "23.98");
    }

    #[test]
    fn format_sample_rate_khz_examples() {
        assert_eq!(format_sample_rate_khz(48000), "48 kHz");
        assert_eq!(format_sample_rate_khz(96000), "96 kHz");
        assert_eq!(format_sample_rate_khz(44100), "44.1 kHz");
    }

    #[test]
    fn default_group_hint_includes_role_and_element_name() {
        gst::init().unwrap();
        let pipeline = gst::Pipeline::builder().name("pipeline0").build();
        let element = gst::Bin::builder().name("mxlsink0").build();
        pipeline.add(&element).unwrap();
        let hint = default_group_hint("Video", element.upcast_ref());
        assert!(hint.ends_with(":Video mxlsink0"), "unexpected hint: {hint}");
        assert!(
            hint.contains(" pipeline0:"),
            "pipeline name missing from group: {hint}"
        );
        assert!(hint.starts_with("Media Function "));

        let element = gst::Bin::builder().name("sink:cam").build();
        let hint = default_group_hint("Audio", element.upcast_ref());
        assert!(hint.ends_with(":Audio sink-cam"), "unexpected hint: {hint}");
        assert!(
            !hint.contains(" pipeline"),
            "pipeline-less hint should omit pipeline token: {hint}"
        );
    }

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
        let default_name = format!(
            "MXL Video Flow, {}p{}",
            height,
            format_framerate(framerate.numer(), framerate.denom())
        );
        let mut tags = HashMap::new();
        tags.insert(
            GROUPHINT_TAG.to_string(),
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
            description: default_name.clone(),
            tags,
            format: "urn:x-nmos:format:video".into(),
            label: default_name,
            parents: vec![],
            media_type: format!("video/{}", format),
            details: FlowDefDetails::Video(flow_def_details),
        };

        let json = serde_json::to_value(&flow_def)
            .map_err(|_| glib::Error::new(CoreError::Failed, "Failed to convert to json"))?;

        let expected_json = serde_json::json!({
            "description": "MXL Video Flow, 1080p29.97",
            "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ed",
            "tags": {"urn:x-nmos:tag:grouphint/v1.0": ["Media Function XYZ:Audio"]},
            "format": "urn:x-nmos:format:video",
            "label": "MXL Video Flow, 1080p29.97",
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

    #[test]
    fn resolve_flow_metadata_keeps_defaults_when_unset() {
        let settings = Settings::default();
        let (label, description, tags) =
            resolve_flow_metadata(&settings, "default-name".into(), "default:Video".into());
        assert_eq!(label, "default-name");
        assert_eq!(description, "default-name");
        assert_eq!(
            tags.get(GROUPHINT_TAG).map(Vec::as_slice),
            Some(["default:Video".to_string()].as_slice())
        );
    }

    #[test]
    fn resolve_flow_metadata_applies_property_overrides() {
        let settings = Settings {
            label: "Studio A camera".into(),
            description: "v210 1080p50".into(),
            group_hint: "Camera:Video".into(),
            ..Settings::default()
        };
        let (label, description, tags) =
            resolve_flow_metadata(&settings, "default-name".into(), "default:Video".into());
        assert_eq!(label, "Studio A camera");
        assert_eq!(description, "v210 1080p50");
        assert_eq!(
            tags.get(GROUPHINT_TAG).map(Vec::as_slice),
            Some(["Camera:Video".to_string()].as_slice())
        );
    }
}
