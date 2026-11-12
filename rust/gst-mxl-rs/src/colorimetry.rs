// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

//! Mapping between GStreamer video colorimetry and the `colorspace`,
//! `transfer_characteristic` and `component_range` attributes of a video flow
//! definition.

use gst_video::{
    VideoColorMatrix, VideoColorPrimaries, VideoColorRange, VideoColorimetry, VideoTransferFunction,
};
use gstreamer_video as gst_video;

/// Flow definition attributes that describe the colorimetry of a video flow.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct FlowColorimetry {
    pub colorspace: &'static str,
    pub transfer_characteristic: &'static str,
    pub component_range: &'static str,
}

/// The flow definition attributes for `colorimetry`, or `None` when they
/// cannot express it.
pub(crate) fn flow_colorimetry_from_gst(colorimetry: &VideoColorimetry) -> Option<FlowColorimetry> {
    let component_range = match colorimetry.range() {
        VideoColorRange::Range16_235 => "NARROW",
        VideoColorRange::Range0_255 => "FULL",
        _ => return None,
    };
    let transfer_characteristic = match colorimetry.transfer() {
        VideoTransferFunction::Bt709
        | VideoTransferFunction::Bt601
        | VideoTransferFunction::Smpte240m
        | VideoTransferFunction::Bt202010
        | VideoTransferFunction::Bt202012 => "SDR",
        VideoTransferFunction::Smpte2084 => "PQ",
        VideoTransferFunction::AribStdB67 => "HLG",
        _ => return None,
    };
    let colorspace = match (colorimetry.matrix(), colorimetry.primaries()) {
        (VideoColorMatrix::Bt601, VideoColorPrimaries::Smpte170m) => "BT601",
        (VideoColorMatrix::Bt709, VideoColorPrimaries::Bt709) => "BT709",
        (VideoColorMatrix::Smpte240m, VideoColorPrimaries::Smpte240m) => "SMPTE240M",
        (VideoColorMatrix::Bt2020, VideoColorPrimaries::Bt2020)
            if transfer_characteristic == "SDR" =>
        {
            "BT2020"
        }
        (VideoColorMatrix::Bt2020, VideoColorPrimaries::Bt2020) => "BT2100",
        _ => return None,
    };
    Some(FlowColorimetry {
        colorspace,
        transfer_characteristic,
        component_range,
    })
}

/// The GStreamer colorimetry for the flow definition attributes, or `None`
/// when GStreamer cannot express them. An absent `transfer_characteristic` is
/// `SDR`, the NMOS default, and an absent `component_range` is `NARROW`, the
/// default for `video/v210`. `bit_depth` is the largest component bit depth,
/// which selects the BT.2020 SDR transfer function.
pub(crate) fn gst_colorimetry_from_flow(
    colorspace: &str,
    transfer_characteristic: Option<&str>,
    component_range: Option<&str>,
    bit_depth: Option<u8>,
) -> Option<VideoColorimetry> {
    let range = match component_range.unwrap_or("NARROW") {
        "NARROW" => VideoColorRange::Range16_235,
        // GStreamer has no range that excludes the SMPTE RP 2077 protected
        // values, and FULLPROTECT samples are within full range.
        "FULL" | "FULLPROTECT" => VideoColorRange::Range0_255,
        _ => return None,
    };
    let (matrix, primaries) = match colorspace {
        "BT601" => (VideoColorMatrix::Bt601, VideoColorPrimaries::Smpte170m),
        "BT709" => (VideoColorMatrix::Bt709, VideoColorPrimaries::Bt709),
        "SMPTE240M" => (VideoColorMatrix::Smpte240m, VideoColorPrimaries::Smpte240m),
        "BT2020" | "BT2100" => (VideoColorMatrix::Bt2020, VideoColorPrimaries::Bt2020),
        _ => return None,
    };
    let transfer = match transfer_characteristic.unwrap_or("SDR") {
        "SDR" => match matrix {
            VideoColorMatrix::Bt601 => VideoTransferFunction::Bt601,
            VideoColorMatrix::Bt709 => VideoTransferFunction::Bt709,
            VideoColorMatrix::Smpte240m => VideoTransferFunction::Smpte240m,
            // Rec. ITU-R BT.2020 has different constants for 12-bit systems.
            _ if bit_depth.is_some_and(|d| d >= 12) => VideoTransferFunction::Bt202012,
            _ => VideoTransferFunction::Bt202010,
        },
        "PQ" => VideoTransferFunction::Smpte2084,
        "HLG" => VideoTransferFunction::AribStdB67,
        _ => return None,
    };
    Some(VideoColorimetry::new(range, matrix, transfer, primaries))
}

#[cfg(test)]
mod tests {
    use std::str::FromStr;

    use super::*;

    fn gst(colorimetry: &str) -> VideoColorimetry {
        gstreamer::init().unwrap();
        VideoColorimetry::from_str(colorimetry).unwrap()
    }

    fn flow(
        colorspace: &'static str,
        transfer_characteristic: &'static str,
        component_range: &'static str,
    ) -> Option<FlowColorimetry> {
        Some(FlowColorimetry {
            colorspace,
            transfer_characteristic,
            component_range,
        })
    }

    #[test]
    fn colorimetries_round_trip() {
        for (name, colorspace, transfer_characteristic, component_range) in [
            ("bt601", "BT601", "SDR", "NARROW"),
            ("bt709", "BT709", "SDR", "NARROW"),
            ("smpte240m", "SMPTE240M", "SDR", "NARROW"),
            ("bt2020-10", "BT2020", "SDR", "NARROW"),
            ("bt2100-pq", "BT2100", "PQ", "NARROW"),
            ("bt2100-hlg", "BT2100", "HLG", "NARROW"),
            // full range BT.709
            ("1:3:5:1", "BT709", "SDR", "FULL"),
            // full range BT.2100 PQ
            ("1:6:14:7", "BT2100", "PQ", "FULL"),
        ] {
            assert_eq!(
                flow_colorimetry_from_gst(&gst(name)),
                flow(colorspace, transfer_characteristic, component_range),
                "{name}"
            );
            assert_eq!(
                gst_colorimetry_from_flow(
                    colorspace,
                    Some(transfer_characteristic),
                    Some(component_range),
                    Some(10)
                )
                .map(|c| c.to_string()),
                Some(name.to_string()),
                "{colorspace} {transfer_characteristic} {component_range}"
            );
        }
    }

    #[test]
    fn other_gst_colorimetries_map_to_the_same_attributes() {
        // BT.2020 12-bit transfer
        assert_eq!(
            flow_colorimetry_from_gst(&gst("bt2020")),
            flow("BT2020", "SDR", "NARROW")
        );
    }

    #[test]
    fn component_bit_depth_selects_the_bt2020_transfer() {
        for (colorspace, bit_depth, name) in [
            ("BT2020", None, "bt2020-10"),
            ("BT2020", Some(10), "bt2020-10"),
            ("BT2020", Some(12), "bt2020"),
            ("BT2100", Some(12), "bt2020"),
        ] {
            assert_eq!(
                gst_colorimetry_from_flow(colorspace, None, None, bit_depth).map(|c| c.to_string()),
                Some(name.to_string()),
                "{colorspace} {bit_depth:?}"
            );
        }
    }

    #[test]
    fn fullprotect_flows_are_full_range() {
        for colorspace in ["BT709", "BT2100"] {
            assert_eq!(
                gst_colorimetry_from_flow(colorspace, None, Some("FULLPROTECT"), None),
                gst_colorimetry_from_flow(colorspace, None, Some("FULL"), None),
                "{colorspace}"
            );
        }
    }

    #[test]
    fn absent_attributes_use_the_defaults() {
        assert_eq!(
            gst_colorimetry_from_flow("BT709", None, None, None).map(|c| c.to_string()),
            Some("bt709".to_string())
        );
        assert_eq!(
            gst_colorimetry_from_flow("BT2100", None, None, None).map(|c| c.to_string()),
            Some("bt2020-10".to_string())
        );
    }

    #[test]
    fn inexpressible_gst_colorimetries_are_refused() {
        for name in [
            // unknown range
            "0:3:5:1", // BT.601 625-line primaries
            "2:4:5:3", "sRGB",
        ] {
            assert_eq!(flow_colorimetry_from_gst(&gst(name)), None, "{name}");
        }
    }

    #[test]
    fn inexpressible_flow_attributes_are_refused() {
        for (colorspace, transfer_characteristic, component_range) in [
            ("BT709", None, Some("full")),
            ("XYZ", None, None),
            ("UNSPECIFIED", None, None),
            ("bt709", None, None),
            ("BT2100", Some("LINEAR"), None),
        ] {
            assert_eq!(
                gst_colorimetry_from_flow(
                    colorspace,
                    transfer_characteristic,
                    component_range,
                    None
                ),
                None,
                "{colorspace} {transfer_characteristic:?} {component_range:?}"
            );
        }
    }
}
