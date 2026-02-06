<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Configuration: Flow definition JSON schema

Each flow requires a flow definition JSON file that describes its format. MXL uses NMOS IS-04 flow definitions with extensions.

## General structure

All flow definitions share common fields:

```json
{
  "id": "uuid-string",              // Required: unique flow ID
  "description": "human readable",  // Optional
  "label": "short name",            // Optional
  "tags": {                         // Optional: metadata tags
    "urn:x-nmos:tag:grouphint/v1.0": ["GroupName:FlowType"]
  },
  "format": "urn:x-nmos:format:TYPE",  // Required: video, audio, or data
  "media_type": "mime/type",           // Required: specific codec/format
  "parents": [],                       // Optional: parent flow IDs
  // Format-specific fields follow
}
```

## Video flow configuration (v210)

**Resolution: 1920x1080, progressive, 29.97 fps**

```json
{
  "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ed",
  "description": "1080p29.97 v210 video",
  "label": "1080p29.97",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": ["MyApp:Video"]
  },
  "format": "urn:x-nmos:format:video",
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
    {"name": "Y",  "width": 1920, "height": 1080, "bit_depth": 10},
    {"name": "Cb", "width": 960,  "height": 1080, "bit_depth": 10},
    {"name": "Cr", "width": 960,  "height": 1080, "bit_depth": 10}
  ]
}
```

**Other common video resolutions:**

720p60:
```json
{
  "frame_width": 1280,
  "frame_height": 720,
  "grain_rate": {"numerator": 60, "denominator": 1}
}
```

2160p (4K) 50fps:
```json
{
  "frame_width": 3840,
  "frame_height": 2160,
  "grain_rate": {"numerator": 50, "denominator": 1}
}
```

## Video flow with alpha (v210a)

v210a adds an alpha component to the components array:

```json
{
  "id": "a1b2c3d4-e5f6-4789-a012-345678901234",
  "description": "1080p30 v210a with alpha",
  "label": "1080p30 w/ Alpha",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": ["MyApp:VideoAlpha"]
  },
  "format": "urn:x-nmos:format:video",
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
    {"name": "Y",  "width": 1920, "height": 1080, "bit_depth": 10},
    {"name": "Cb", "width": 960,  "height": 1080, "bit_depth": 10},
    {"name": "Cr", "width": 960,  "height": 1080, "bit_depth": 10},
    {"name": "A",  "width": 1920, "height": 1080, "bit_depth": 10}
  ]
}
```

The alpha component ("A") is always full resolution (not subsampled).

## Audio flow configuration (float32)

**48 kHz, 8 channels:**

```json
{
  "id": "b3bb5be7-9fe9-4324-a5bb-4c70e1084449",
  "description": "48kHz 8-channel float32",
  "label": "48k 8ch",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": ["MyApp:Audio"]
  },
  "format": "urn:x-nmos:format:audio",
  "media_type": "audio/float32",
  "sample_rate": {
    "numerator": 48000
  },
  "channel_count": 8,
  "bit_depth": 32
}
```

**Important notes:**

- `channel_count` is an MXL extension (not in standard NMOS IS-04). It is **required** for audio flows.
- `bit_depth` is always 32 for float32 format.
- `sample_rate` uses only the numerator field (denominator is implicitly 1).

**Other common audio configurations:**

Stereo 48kHz:
```json
{
  "sample_rate": {"numerator": 48000},
  "channel_count": 2,
  "bit_depth": 32
}
```

5.1 Surround 96kHz:
```json
{
  "sample_rate": {"numerator": 96000},
  "channel_count": 6,
  "bit_depth": 32
}
```

16-channel 48kHz:
```json
{
  "sample_rate": {"numerator": 48000},
  "channel_count": 16,
  "bit_depth": 32
}
```

## Ancillary data flow configuration

SMPTE ST 291 ancillary data:

```json
{
  "id": "anc-data-flow-uuid",
  "description": "SMPTE ST 291 ancillary data",
  "label": "ANC",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": ["MyApp:ANC"]
  },
  "format": "urn:x-nmos:format:data",
  "media_type": "video/smpte291",
  "grain_rate": {
    "numerator": 30000,
    "denominator": 1001
  }
}
```

**Key points:**

- Format is "data" (not "video"), but media_type is "video/smpte291"
- `grain_rate` typically matches the associated video flow
- No width/height fields (data flows don't have dimensions)

[Back to Configuration overview](./Configuration.md)
