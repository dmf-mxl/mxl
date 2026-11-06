// SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#[cfg(test)]
mod tests {
    use std::collections::HashMap;

    use crate::mxlsink::render_continuous::{AlignmentLimits, Placement, SampleAlignment};
    use crate::mxlsink::render_discrete::{GrainAlignment, GrainClock};
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
    #[test]
    fn mxl_timestamp_adds_signed_running_time() {
        use crate::mxlsink::imp::mxl_timestamp;
        let anchor = 1_000_000_000i128;
        let ms = gst::ClockTime::from_mseconds;
        assert_eq!(
            mxl_timestamp(anchor, gst::Signed::Positive(ms(20))),
            Some(1_020_000_000)
        );
        // A buffer that starts before its segment lands just before the anchor.
        assert_eq!(
            mxl_timestamp(anchor, gst::Signed::Negative(ms(20))),
            Some(980_000_000)
        );
        // Results outside the u64 MXL timestamp range are rejected, not wrapped.
        assert_eq!(
            mxl_timestamp(
                anchor,
                gst::Signed::Negative(gst::ClockTime::from_seconds(2))
            ),
            None
        );
        assert_eq!(
            mxl_timestamp(
                u64::MAX as i128,
                gst::Signed::Positive(gst::ClockTime::from_nseconds(1))
            ),
            None
        );
    }

    /// Limits used by most placement tests: 1920 samples (40 ms at 48 kHz)
    /// and no wait.
    const NO_WAIT: AlignmentLimits = AlignmentLimits {
        threshold: 1920,
        discont_wait: 0,
    };

    /// Limits with a wait of 48 000 samples (1 s at 48 kHz).
    const ONE_SECOND_WAIT: AlignmentLimits = AlignmentLimits {
        threshold: 1920,
        discont_wait: 48_000,
    };

    fn at(next_index: u64) -> SampleAlignment {
        SampleAlignment {
            next_index: Some(next_index),
            discont_start: None,
        }
    }

    #[test]
    fn place_buffer_first_buffer_uses_timestamp() {
        assert_eq!(
            SampleAlignment::default().place(1000, 1024, false, &NO_WAIT),
            Placement::Write {
                index: 1000,
                skip: 0
            }
        );
    }

    #[test]
    fn place_buffer_small_overlap_and_gap_stay_contiguous() {
        assert_eq!(
            at(1024).place(1008, 1024, false, &NO_WAIT),
            Placement::Write {
                index: 1024,
                skip: 0
            }
        );
        assert_eq!(
            at(3040).place(3072, 1024, false, &NO_WAIT),
            Placement::Write {
                index: 3040,
                skip: 0
            }
        );
        assert_eq!(
            at(10_000).place(10_000 + 1920, 1024, false, &NO_WAIT),
            Placement::Write {
                index: 10_000,
                skip: 0
            }
        );
    }

    #[test]
    fn place_buffer_large_gap_resyncs_to_timestamp() {
        assert_eq!(
            at(10_000).place(10_000 + 1921, 1024, false, &NO_WAIT),
            Placement::Write {
                index: 11_921,
                skip: 0
            }
        );
    }

    #[test]
    fn place_buffer_large_overlap_trims_or_drops() {
        // Starts 2000 samples before the next index: the first 2000 are dropped.
        assert_eq!(
            at(10_000).place(8_000, 4096, false, &NO_WAIT),
            Placement::Write {
                index: 10_000,
                skip: 2000
            }
        );
        // Ends at or before the next index: nothing is left to write.
        assert_eq!(
            at(10_000).place(5_000, 4096, false, &NO_WAIT),
            Placement::Drop
        );
        assert_eq!(
            at(10_000).place(5_904, 4096, false, &NO_WAIT),
            Placement::Drop
        );
    }

    #[test]
    fn place_buffer_discont_resyncs_within_threshold() {
        assert_eq!(
            at(10_000).place(10_960, 1024, true, &NO_WAIT),
            Placement::Write {
                index: 10_960,
                skip: 0
            }
        );
        // Samples already written are still not written again.
        assert_eq!(
            at(10_000).place(9_500, 1024, true, &NO_WAIT),
            Placement::Write {
                index: 10_000,
                skip: 500
            }
        );
    }

    #[test]
    fn place_buffer_waits_before_resyncing() {
        let mut alignment = at(10_000);
        // 2400 samples late, beyond the threshold: still written contiguously,
        // and the wait starts at the index the buffer should have had.
        assert_eq!(
            alignment.place(12_400, 1024, false, &ONE_SECOND_WAIT),
            Placement::Write {
                index: 10_000,
                skip: 0
            }
        );
        assert_eq!(alignment.discont_start, Some(10_000));
        // Back within the threshold: the wait is abandoned.
        alignment.next_index = Some(11_024);
        assert_eq!(
            alignment.place(11_024, 1024, false, &ONE_SECOND_WAIT),
            Placement::Write {
                index: 11_024,
                skip: 0
            }
        );
        assert_eq!(alignment.discont_start, None);
    }

    #[test]
    fn place_buffer_resyncs_after_discont_wait() {
        // Timestamps have stayed 2400 samples late since index 10 000. While
        // they are less than 48 000 samples past that, buffers follow on.
        let mut alignment = SampleAlignment {
            next_index: Some(55_056),
            discont_start: Some(10_000),
        };
        assert_eq!(
            alignment.place(57_456, 1024, false, &ONE_SECOND_WAIT),
            Placement::Write {
                index: 55_056,
                skip: 0
            }
        );
        assert_eq!(alignment.discont_start, Some(10_000));
        // The next buffer is past the wait, so the sink resyncs to it.
        alignment.next_index = Some(56_080);
        assert_eq!(
            alignment.place(58_480, 1024, false, &ONE_SECOND_WAIT),
            Placement::Write {
                index: 58_480,
                skip: 0
            }
        );
        assert_eq!(alignment.discont_start, None);
        // A single jump of at least the wait resyncs at once.
        alignment.next_index = Some(10_000);
        assert_eq!(
            alignment.place(58_000, 1024, false, &ONE_SECOND_WAIT),
            Placement::Write {
                index: 58_000,
                skip: 0
            }
        );
        assert_eq!(alignment.discont_start, None);
    }

    /// Grain rate of 25 per second, so one grain is 40 ms.
    const GRAIN_NS: u64 = 40_000_000;

    /// `GrainClock` at 25 grains per second that rounds to the nearest index,
    /// like the MXL library.
    struct TestClock;

    impl GrainClock for TestClock {
        fn timestamp_to_index(&self, timestamp: u64) -> Result<u64, gst::FlowError> {
            Ok((timestamp + GRAIN_NS / 2) / GRAIN_NS)
        }

        fn index_to_timestamp(&self, index: u64) -> Result<u64, gst::FlowError> {
            Ok(index * GRAIN_NS)
        }
    }

    const MS: u64 = 1_000_000;

    #[test]
    fn grain_index_records_first_grain_phase() {
        let mut alignment = GrainAlignment::default();
        assert_eq!(
            alignment.grain_index(100 * GRAIN_NS + 15 * MS, &TestClock),
            Ok(100)
        );
        assert_eq!(alignment.phase, Some(15_000_000));

        let mut alignment = GrainAlignment::default();
        assert_eq!(
            alignment.grain_index(100 * GRAIN_NS - 15 * MS, &TestClock),
            Ok(100)
        );
        assert_eq!(alignment.phase, Some(-15_000_000));
    }

    #[test]
    fn grain_index_rounds_relative_to_phase() {
        let mut alignment = GrainAlignment::default();
        alignment
            .grain_index(100 * GRAIN_NS + 15 * MS, &TestClock)
            .unwrap();
        // 25 ms past grain 101 would round to 102 without the phase. Relative
        // to the first grain it is 10 ms late, so it stays at 101.
        assert_eq!(
            alignment.grain_index(101 * GRAIN_NS + 25 * MS, &TestClock),
            Ok(101)
        );
        // Jitter of up to half a grain either side keeps consecutive indices.
        assert_eq!(
            alignment.grain_index(102 * GRAIN_NS + 15 * MS - 19 * MS, &TestClock),
            Ok(102)
        );
        assert_eq!(
            alignment.grain_index(103 * GRAIN_NS + 15 * MS + 19 * MS, &TestClock),
            Ok(103)
        );
        assert_eq!(alignment.phase, Some(15_000_000));
    }

    #[test]
    fn place_grain_writes_at_timestamp_index() {
        let at = |next_index| GrainAlignment {
            next_index,
            phase: None,
        };
        assert_eq!(at(None).place(1000), Some(1000));
        assert_eq!(at(Some(1000)).place(1000), Some(1000));
        // A gap is not closed: the grain keeps its own index.
        assert_eq!(at(Some(1000)).place(1001), Some(1001));
        assert_eq!(at(Some(1000)).place(5000), Some(5000));
    }

    #[test]
    fn place_grain_drops_already_written_index() {
        let alignment = GrainAlignment {
            next_index: Some(1000),
            phase: None,
        };
        assert_eq!(alignment.place(999), None);
        assert_eq!(alignment.place(10), None);
    }
}
