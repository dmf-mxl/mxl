<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Flow Definition JSON Examples

MXL uses NMOS IS-04 flow definitions with some extensions. Here are complete examples for every supported format.

## Video flow (v210, 1920x1080p29.97)

```json
{
  "description": "MXL Video Flow, 1080p29.97",
  "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ed",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "MyMediaFunction:Video"
    ]
  },
  "format": "urn:x-nmos:format:video",
  "label": "1080p29.97 v210",
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
}
```

## Video flow with alpha (v210a, 1920x1080p30)

```json
{
  "description": "MXL Video Flow with Alpha, 1080p30",
  "id": "a1b2c3d4-e5f6-4789-a012-345678901234",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "MyMediaFunction:VideoAlpha"
    ]
  },
  "format": "urn:x-nmos:format:video",
  "label": "1080p30 v210a",
  "parents": [],
  "media_type": "video/v210a",
  "grain_rate": {
    "numerator": 30,
    "denominator": 1
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
    },
    {
      "name": "A",
      "width": 1920,
      "height": 1080,
      "bit_depth": 10
    }
  ]
}
```

## Audio flow (float32, 48kHz, 8 channels)

```json
{
  "description": "MXL Audio Flow, 48kHz 8-channel",
  "id": "b3bb5be7-9fe9-4324-a5bb-4c70e1084449",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "MyMediaFunction:Audio"
    ]
  },
  "format": "urn:x-nmos:format:audio",
  "label": "48kHz 8ch float32",
  "parents": [],
  "media_type": "audio/float32",
  "sample_rate": {
    "numerator": 48000
  },
  "channel_count": 8,
  "bit_depth": 32
}
```

Note: `channel_count` is an MXL-specific extension to NMOS IS-04. It is required for audio flows.

## Audio flow (float32, 96kHz, stereo)

```json
{
  "description": "MXL Audio Flow, 96kHz stereo",
  "id": "96000hz-stereo-flow-uuid-here",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "MyMediaFunction:Audio96k"
    ]
  },
  "format": "urn:x-nmos:format:audio",
  "label": "96kHz stereo float32",
  "parents": [],
  "media_type": "audio/float32",
  "sample_rate": {
    "numerator": 96000
  },
  "channel_count": 2,
  "bit_depth": 32
}
```

## Ancillary data flow (SMPTE ST 291)

```json
{
  "description": "MXL Ancillary Data Flow",
  "id": "anc-data-flow-uuid-here",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "MyMediaFunction:ANC"
    ]
  },
  "format": "urn:x-nmos:format:data",
  "label": "SMPTE ST 291 ANC",
  "parents": [],
  "media_type": "video/smpte291",
  "grain_rate": {
    "numerator": 30000,
    "denominator": 1001
  }
}
```

## Additional resources

For more flow definition examples, see the test data in `lib/tests/data/`.

---

Back to [Usage overview](./Usage.md)
