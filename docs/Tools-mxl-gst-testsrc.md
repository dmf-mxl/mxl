<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools: mxl-gst-testsrc

A test signal generator that produces video and/or audio grains using GStreamer's `videotestsrc` and `audiotestsrc` elements, writing them to MXL flows.

## When would I use this tool?

- **Testing:** Generate test patterns and tones for validating readers
- **Development:** Create flows without real media sources
- **Benchmarking:** Measure reader performance with synthetic data
- **Pipeline testing:** Verify A/V sync and flow alignment

## Usage

```bash
./mxl-gst-testsrc [OPTIONS]

OPTIONS:
  -h,     --help              Print this help message and exit
  -v,     --video-config-file TEXT
                              The json file which contains the Video NMOS Flow configuration.
          --video-options-file TEXT
                              The json file which contains the Video Flow options.
  -a,     --audio-config-file TEXT
                              The json file which contains the Audio NMOS Flow configuration
          --audio-options-file TEXT
                              The json file which contains the Audio Flow options.
          --audio-offset INT [0]
                              Audio sample offset in number of samples. Positive value means
                              you are adding a delay (commit in the past).
          --video-offset INT [0]
                              Video frame offset in number of frames. Positive value means you
                              are adding a delay (commit in the past).
  -d,     --domain TEXT:DIR REQUIRED
                              The MXL domain directory
  -p,     --pattern TEXT [smpte]
                              Test pattern to use. For available options see
                              https://gstreamer.freedesktop.org/documentation/videotestsrc/index.html?gi-language=c#GstVideoTestSrcPattern
  -t,     --overlay-text TEXT [EBU DMF MXL]
                              Change the text overlay of the test source
  -g,     --group-hint TEXT [mxl-gst-testsrc-group]
                              The group-hint value to use in the flow json definition
```

## Flow definition files

### Video flow (v210_flow.json)

```json
{
  "description": "MXL Test Flow, 1080p29",
  "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ed",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "My Media Function Unique Name (Change Me):Video"
    ]
  },
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
}
```

### Audio flow (audio_flow.json)

```json
{
  "description": "MXL Audio Flow",
  "format": "urn:x-nmos:format:audio",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "My Media Function Unique Name (Change Me):Audio"
    ]
  },
  "label": "MXL Audio Flow",
  "version": "1441812152:154331951",
  "id": "b3bb5be7-9fe9-4324-a5bb-4c70e1084449",
  "media_type": "audio/float32",
  "sample_rate": {
    "numerator": 48000
  },
  "channel_count": 2,
  "bit_depth": 32,
  "parents": [],
  "source_id": "2aa143ac-0ab7-4d75-bc32-5c00c13d186f",
  "device_id": "169feb2c-3fae-42a5-ae2e-f6f8cbce29cf"
}
```

Note: `channel_count` is an MXL-specific extension (not found in standard NMOS definition). To adjust the number of audio channels, update the `channel_count` value.

## Examples

**Example 1: Video only**

```bash
./mxl-gst-testsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json
```

**Example 2: Audio only**

```bash
./mxl-gst-testsrc \
  -d /dev/shm \
  -a lib/tests/data/audio_flow.json
```

**Example 3: Video and audio together**

```bash
./mxl-gst-testsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json \
  -a lib/tests/data/audio_flow.json
```

**Example 4: Custom test pattern**

```bash
./mxl-gst-testsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json \
  -p ball \
  -t "My Custom Text"
```

Available patterns: smpte, snow, black, white, red, green, blue, checkers-1, checkers-2, checkers-4, checkers-8, circular, blink, smpte75, zone-plate, gamut, chroma-zone-plate, solid-color, ball, pinwheel, spokes, gradient, colors

**Example 5: Adjust batch sizes with options files**

By default, `videotestsrc` produces one slice and one sample at a time because it relies on the `maxSyncBatchSizeHint` field in the flow options to determine batch size, which defaults to 1 when not configured. To modify this behavior, create separate flow option files for video and audio.

video_options.json:
```json
{
  "maxCommitBatchSizeHint": 60,
  "maxSyncBatchSizeHint": 60
}
```

audio_options.json:
```json
{
  "maxCommitBatchSizeHint": 512,
  "maxSyncBatchSizeHint": 512
}
```

Run with options:
```bash
./mxl-gst-testsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json \
  --video-options-file video_options.json \
  -a lib/tests/data/audio_flow.json \
  --audio-options-file audio_options.json
```

## Troubleshooting

**Problem: "GStreamer plugin not found"**

Solution: Install GStreamer base plugins.

```bash
# Ubuntu/Debian
sudo apt-get install gstreamer1.0-plugins-base gstreamer1.0-plugins-good
```

**Problem: High CPU usage**

Solution: Reduce frame rate or resolution in the flow definition, or increase batch sizes.

**Problem: Discontinuities in output**

Solution: Check system load. If CPU is maxed, reduce frame rate or use a lower resolution test pattern.

[Back to Tools overview](./Tools.md)
