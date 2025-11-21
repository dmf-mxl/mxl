<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools

## mxl-info

Simple tool that uses the MXL sdk to open a flow by ID and prints the flow details.

```bash
Usage: ./mxl-info [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
  -d,--domain TEXT:DIR REQUIRED
                              The MXL domain directory
  -f,--flow TEXT              The flow id to analyse
  -l,--list                   List all flows in the MXL domain
```

Example 1 : listing all flows in a domain

```bash
./mxl-info -d ~/mxl_domain/ -l
--- MXL Flows ---
        5fbec3b1-1b0f-417d-9059-8b94a47197ed

```

Example 2 : Printing details about a specific flow

```bash
./mxl-info -d ~/mxl_domain/ -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed

- Flow [5fbec3b1-1b0f-417d-9059-8b94a47197ed]
        version           : 1
        struct size       : 4192
        flags             : 0x00000000000000000000000000000000
        head index        : 52146817788
        grain rate        : 60000/1001
        grain count       : 3
        last write time   : 2025-02-19 11:44:12 UTC +062314246ns
        last read time    : 1970-01-01 00:00:00 UTC +000000000ns
```

Hint : Live monitoring of a flow details (updates every second)

```bash
watch -n 1 -p ./mxl-info -d ~/mxl_domain/ -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

## mxl-viewer

TODO. A generic GUI application based on gstreamer or ffmpeg to display flow(s).

## mxl-gst-videotestsrc

A binary that uses the gstreamer element 'videotestsrc' to produce video grains and/or audio samples which will be pushed to a MXL Flow. The flow is configured from a NMOS Flow json file. Here's an example of such file for a video flow:

```json
{
  "$copyright": "SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.",
  "$license": "SPDX-License-Identifier: Apache-2.0",
  "description": "MXL Test Flow, 1080p29",
  "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ed",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "Media Function XYZ:Video"
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

And here is an example of the NMOS Flow json file for an audio flow. To modify the number of audio channels present in the flow, modify the *"channel_count"* parameter to the desired value.

```json
{
  "$copyright": "SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.",
  "$license": "SPDX-License-Identifier: Apache-2.0",
  "description": "MXL Audio Flow",
  "format": "urn:x-nmos:format:audio",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "Media Function XYZ:Audio"
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
mxl-gst-videotestsrc


./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videotestsrc [OPTIONS]


OPTIONS:
  -h,     --help              Print this help message and exit
  -v,     --video-config-file TEXT
                              The json file which contains the Video NMOS Flow configuration
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
```

Example to run with video only:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videotestsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json
```

Example to run with audio only:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videotestsrc \
  -d /dev/shm \
  -a lib/tests/data/audio_flow.json
```

Example to run with both video and audio:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videotestsrc \
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
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videotestsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json \
  --video-options-file video_options.json \
  -a lib/tests/data/audio_flow.json \
  --audio-options-file audio_options.json
```

## mxl-gst-videosink

A binary that reads from a MXL Flow and display the flow using the gstreamer element 'autovideosink'.

```bash
./build/Linux-GCC-Release/tools/mxl-gst/mxl-gst-videosink [OPTIONS]


OPTIONS:
  -h,     --help              Print this help message and exit
  -v,     --video-flow-id TEXT
                              The video flow ID
  -a,     --audio-flow-id TEXT
                              The audio flow ID
  -d,     --domain TEXT:DIR REQUIRED
                              The MXL domain directory
  -t,     --timeout UINT      The read timeout in ns, frame interval + 1 ms used if not
                              specified
  -l,     --listen-channels UINT [[0,1]]  ...
                              Audio channels to listen
          --audio-offset INT [150000000]
                              Audio playback offset in nanoseconds. Positive value means you
                              are delaying the playback
          --video-offset INT [150000000]
                              Video plaback offset in nanoseconds. Positive value means you are
                              delaying the playback
          --audio-read-delay INT [768]
                              How far in the past/future to read (in audio samples). Positive
                              value means you are delaying the read
          --video-read-delay INT [1]
                              How far in the past/future to read (in video frames). Positive
                              value means you are delaying the read
          --timeout-mode [0]  In timeout mode, the sink uses blocking methods with timeout
                              instead of reading with a defined delay. Currently only video
                              supports this mode and --video-offset is used for both the
                              timeout value and playback offset.

```

Example to run with video only:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videosink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

Example to run with audio only:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videosink \
  -d /dev/shm \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

Example to run with both video and audio:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videosink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

If you experience audio artifacts, try increasing the --audio-offset value. On some systems running a type-2 hypervisor with a host VM, audio glitches have been observed with default settings. Increasing the buffer size can resolve this issue.

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videosink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed \
  --video-offset 1000000000 \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449 \
  --audio-offset 1000000000

```