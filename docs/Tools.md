<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools

## mxl-info

Simple tool that uses the MXL sdk to open a flow by ID and prints the flow details.

```bash
./mxl-info [OPTIONS] [ADDRESS...]

POSITIONALS:
  ADDRESS TEXT ...            MXL URI

OPTIONS:
  -h,     --help              Print this help message and exit
          --version           Display program version information and exit
  -d,     --domain TEXT:DIR   The MXL domain directory
  -f,     --flow TEXT         The flow id to analyse
  -l,     --list              List all flows in the MXL domain
  -g,     --garbage-collect   Garbage collect inactive flows found in the MXL domain

MXL URI format:
mxl://[authority[:port]]/domain[?id=...]
See: https://github.com/dmf-mxl/mxl/docs/Addressability.md
```

Example 1a : listing all flows in a domain using command line options.

```bash
./mxl-info -d ~/mxl_domain/ -l
5fbec3b1-1b0f-417d-9059-8b94a47197ed, "MXL Test Flow, 1080p29 with alpha", "Media Function XYZ:Video"
```

Example 1b : listing all flows in a domain by specifying an MXL domain URI.

```bash
./mxl-info mxl:///dev/shm/mxl
5fbec3b1-1b0f-417d-9059-8b94a47197ed, "MXL Test Flow, 1080p29 with alpha", "Media Function XYZ:Video"
```

Example 2a : Printing details about a specific flow using command line options.

```bash
./mxl-info -d ~/mxl_domain/ -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed

- Flow [5fbec3b1-1b0f-417d-9059-8b94a47197ed]
                   Version: 1
               Struct size: 2048
                    Format: Video
         Grain/sample rate: 30000/1001
         Commit batch size: 1080
           Sync batch size: 1080
          Payload Location: Host
              Device Index: -1
                     Flags: 00000000
               Grain count: 2

                Head index: 52939144165
           Last write time: 1766402776989915517
            Last read time: 1766402716054676488
          Latency (grains): 1
                    Active: true
```

Example 2a : Printing details about a specific flow using an MXL URI 

```bash
./mxl-info mxl:///dev/shm/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed

- Flow [5fbec3b1-1b0f-417d-9059-8b94a47197ed]
                   Version: 1
               Struct size: 2048
                    Format: Video
         Grain/sample rate: 30000/1001
         Commit batch size: 1080
           Sync batch size: 1080
          Payload Location: Host
              Device Index: -1
                     Flags: 00000000
               Grain count: 2

                Head index: 52939144165
           Last write time: 1766402776989915517
            Last read time: 1766402716054676488
          Latency (grains): 1
                    Active: true
```

Hint : Live monitoring of a flow details (updates every second)

```bash
watch -n 1 -p ./mxl-info mxl:///dev/shm/mxl?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

## mxl-gst-testsrc

A binary that uses the gstreamer 'videotestsrc' and 'audiotestsrc' elements to produce video grains and/or audio samples which will be pushed to a MXL Flow. The flow is configured from a NMOS Flow json file. Here's an example of such file :

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
Below is an example of an **augmented NMOS Flow JSON** for audio.  
&emsp; **Note:** The *channel_count* property is a mandatory MXL requirement (not found in standard NMOS definition). To adjust the number of audio channels, simply update the *channel_count* value.

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

Example to run with video only:

```bash
./mxl-gst-testsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json
```

Example to run with audio only:

```bash
./mxl-gst-testsrc \
  -d /dev/shm \
  -a lib/tests/data/audio_flow.json
```

Example to run with both video and audio:

```bash
./mxl-gst-testsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json \
  -a lib/tests/data/audio_flow.json

```

By default, `videotestsrc` produces one slice and one sample at a time because it relies on the`maxSyncBatchSizeHint` field in the flow options to determine batch size, which defaults to 1 when not configured. To modify this behavior, create separate flow option files for video and audio.

video_options.json

```video_options.json
{
  "maxCommitBatchSizeHint": 60,
  "maxSyncBatchSizeHint": 60
}
```

audio_options.json

```audio_options.json
{
  "maxCommitBatchSizeHint": 512,
  "maxSyncBatchSizeHint": 512
}
```

You can run the full example with:

```bash
./mxl-gst-testsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json \
  --video-options-file video_options.json \
  -a lib/tests/data/audio_flow.json \
  --audio-options-file audio_options.json
```

## mxl-gst-sink

A binary that reads from a MXL Flow and display the flow using the gstreamer element 'autovideosink'.

```bash
./mxl-gst-sink [OPTIONS]


OPTIONS:
  -h,     --help              Print this help message and exit
  -v,     --video-flow-id TEXT
                              The video flow ID
  -a,     --audio-flow-id TEXT
                              The audio flow ID
  -d,     --domain TEXT:DIR REQUIRED
                              The MXL domain directory.
  -l,     --listen-channels UINT [[0,1]]  ...
                              Audio channels to listen.
          --read-delay INT [40000000]
                              How far in the past/future to read (in nanoseconds). A positive
                              values means you are delaying the read.
          --playback-delay INT [0]
                              The time in nanoseconds, by which to delay playback of audio
                              and/or video.
          --av-delay INT [0]  The time in nanoseconds, by which to delay the audio relative to
                              video. A positive value means you are delaying audio, a negative
                              value means you are delaying video.
```

Example to run with video only:

```bash
./mxl-gst-sink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

Example to run with audio only:

```bash
./mxl-gst-sink \
  -d /dev/shm \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

Example to run with both video and audio:

```bash
./mxl-gst-sink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

### mxl-gst-looping-filesrc

A utility that continuously loops through MPEG-TS files to generate video grains to push into MXL flows.

#### Prerequisites

This application requires the `looping_filesrc` GStreamer plugin located in `utils/gst-looping-filesrc/`.  After building the plugin successfully, set the GST_PLUGIN_PATH environment variable to enable GStreamer to locate and load the plugin.

```bash
export GST_PLUGIN_PATH="./build/Linux-GCC-Release/utils/gst-looping-filesrc:${GST_PLUGIN_PATH}"
```

**Verify Installation:**

```bash
gst-inspect-1.0 looping_filesrc
```

#### Usage

```bash
./mxl-gst-looping-filesrc [OPTIONS]


OPTIONS:
  -h,     --help              Print this help message and exit
  -d,     --domain TEXT REQUIRED
                              The MXL domain directory
  -i,     --input TEXT:FILE REQUIRED
                              MPEGTS media file location
```

## Gstreamer filters

MXL Source and Sink filters for GStreamer are available [here](../rust/gst-mxl-rs/README.md). 