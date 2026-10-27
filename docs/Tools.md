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
../mxl-info/mxl-info -d /dev/shm/mxl/ -l
CAM1: mxl:///dev/shm/mxl/?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed&id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449
        video : 5fbec3b1-1b0f-417d-9059-8b94a47197ed - Camera 1
        audio : b3bb5be7-9fe9-4324-a5bb-4c70e1084449 - Camera 1

CAM2: mxl:///dev/shm/mxl/?id=53c36c15-10c8-48a3-9ba1-41f31d557fba&id=20e6824e-44b1-43ad-b198-99ebb7fa7092
        video : 53c36c15-10c8-48a3-9ba1-41f31d557fba - Camera 2
        audio : 20e6824e-44b1-43ad-b198-99ebb7fa7092 - Camera 2
```

Example 1b : listing all flows in a domain by specifying an MXL domain URI.

```bash
./mxl-info mxl:///dev/shm/mxl
CAM1: mxl:///dev/shm/mxl/?id=5fbec3b1-1b0f-417d-9059-8b94a47197ed&id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449
        video : 5fbec3b1-1b0f-417d-9059-8b94a47197ed - Camera 1
        audio : b3bb5be7-9fe9-4324-a5bb-4c70e1084449 - Camera 1

CAM2: mxl:///dev/shm/mxl/?id=53c36c15-10c8-48a3-9ba1-41f31d557fba&id=20e6824e-44b1-43ad-b198-99ebb7fa7092
        video : 53c36c15-10c8-48a3-9ba1-41f31d557fba - Camera 2
        audio : 20e6824e-44b1-43ad-b198-99ebb7fa7092 - Camera 2
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

## mxl-video-probe

`mxl-video-probe` keeps a video flow reader open and verifies `--count` grains
starting after the current head. The default count is 60, and the minimum is 60.
The default timeout per grain is 1000 ms. A domain and flow id are required.
They can also be supplied as an MXL URI. The reference file is optional.

```text
Verify video grains and report delivery rate, latency and jitter


mxl-video-probe [OPTIONS] [ADDRESS...]


POSITIONALS:
  ADDRESS TEXT ...            MXL URI

OPTIONS:
  -h,     --help              Print this help message and exit
          --version           Display program version information and exit
  -d,     --domain TEXT:DIR   The MXL domain directory
  -f,     --flow TEXT         The video flow id to read
  -c,     --count UINT:UINT in [60 - 18446744073709551615]
                              Number of video grains to verify, starting after the current head
  -t,     --timeout-ms UINT:UINT in [1 - 18446744073709]
                              Timeout per grain read in milliseconds
          --reference-file TEXT:FILE
                              Optional raw file containing a repeating sequence of complete frames

MXL URI format:
mxl://[authority[:port]]/domain[?id=...]
See: https://github.com/dmf-mxl/mxl/docs/Addressability.md
```

Start the sender before launching the probe. A new video flow starts with head index zero; the probe does not retarget its first read when the sender publishes its first grain.

The probe reads each complete video grain. It checks the grain index, invalid-grain flag and measured delivery rate. When a reference file is supplied, it also checks full payload bytes and reference sequence order. It fails on a read error, a mismatch, or measured elapsed time outside 1% of the expected duration.

The reference file contains a repeating sequence of raw frames with the same format and frame size as the flow. Its length must be a nonzero multiple of the first valid grain's payload size. The probe aligns to the sequence at any starting position, then checks its order and wraps at the end of the file. Repeated frames are allowed; all possible starting positions are retained until the received content distinguishes them. A grain that matches a reference frame outside the expected sequence counts as an order error and starts a new alignment. A grain matching no reference frame counts as a byte error. Invalid grains and byte errors reset alignment.

Static references work without additional options. Repeated copies of a static frame use a single comparison per grain. Identical frames cannot reveal stale or duplicated content published under a new grain index.

Delivery timing uses `std::chrono::steady_clock` timestamps taken after the first and last successful `mxlFlowReaderGetGrain` calls. The expected duration is the number of frame intervals divided by the flow's configured grain rate.

Verification also reports `latency_min_ms`, `latency_mean_ms`, `latency_max_ms`, and `jitter_ms` over all successfully read grains. Each latency sample is the MXL TAI time just after the read returns minus the nominal timestamp of the requested grain index, calculated using the configured grain rate.
Negative values mean the grain is dated in the future. Jitter is the maximum latency minus the minimum latency over the run. These statistics do not set additional pass or fail thresholds.

Latency includes reader scheduling delays, payload verification time between reads, and any backlog. MXL rounds origin timestamps to the nearest grain index, so reconstructing an origin timestamp can introduce up to half a frame interval of error. This does not measure end-to-end presentation latency. Clock adjustments during sampling can affect the results.

For a v210 `blink` flow, generate the reference using the same dimensions and rate as the sender. A GStreamer pipeline can publish the flow without a custom sender application:

```sh
gst-launch-1.0 -q videotestsrc num-buffers=2 pattern=blink ! \
  video/x-raw,format=v210,width=1920,height=1080,framerate=60/1 ! \
  filesink location=/tmp/blink-1080p60.v210

gst-launch-1.0 -q videotestsrc is-live=true pattern=blink ! \
  video/x-raw,format=v210,width=1920,height=1080,framerate=60/1 ! \
  mxlsink domain=/dev/shm/mxl flow-id=FLOW_ID sync=false

mxl-video-probe -d /dev/shm/mxl -f FLOW_ID --count 10800 \
  --reference-file /tmp/blink-1080p60.v210
```

For a static white flow, use a reference file generated from the same pattern:

```sh
gst-launch-1.0 -q videotestsrc num-buffers=1 pattern=white ! \
  video/x-raw,format=v210,width=1920,height=1080,framerate=60/1 ! \
  filesink location=/tmp/white-1080p60.v210

gst-launch-1.0 -q videotestsrc is-live=true pattern=white ! \
  video/x-raw,format=v210,width=1920,height=1080,framerate=60/1 ! \
  mxlsink domain=/dev/shm/mxl flow-id=FLOW_ID sync=false

mxl-video-probe -d /dev/shm/mxl -f FLOW_ID --count 10800 \
  --reference-file /tmp/white-1080p60.v210
```

GStreamer generates each blink frame; static patterns such as white reuse a cached image with a copy per frame. Sender capacity may differ from a sender that wraps prebuilt frame buffers. The stock `mxl-gst-testsrc` adds changing text and clock overlays, so its output does not match the fixed reference file.

## mxl-data-probe

A tool that opens an MXL ancillary Data flow (`media_type: video/smpte291`) or an Event flow (`urn:x-nmos:format:event`).
For Data flows, it reads grains starting at the current head index and prints their RFC-8331 ANC elements.
For Event flows, it reads in queue order starting at the oldest retained event and prints the timestamp, flags, registry, data item type, payload size, fragment offset, completion flag, and payload bytes in hexadecimal. Events do not require a `media_type`.

`--count` counts individual stored entries, including fragments. Fragments are displayed separately without reassembly or continuity validation. The printed event number is a local read counter starting at zero. A read error (including a timeout or ring overrun) stops the probe with a nonzero exit status.

```bash
./mxl-data-probe [OPTIONS] [ADDRESS...]

POSITIONALS:
  ADDRESS TEXT ...            MXL URI

OPTIONS:
  -h,     --help              Print this help message and exit
          --version           Display program version information and exit
  -d,     --domain TEXT:DIR   The MXL domain directory
  -f,     --flow TEXT         The flow id to read
  -c,     --count UINT [1]    Number of grains (from the head) or event entries
                              (from the oldest retained) to read
  -t,     --timeout-ms UINT [1000]
                              Timeout per grain or event read in milliseconds

MXL URI format:
mxl://[authority[:port]]/domain[?id=...]
See: https://github.com/dmf-mxl/mxl/docs/Addressability.md
```

Example using command line options:

```bash
./mxl-data-probe -d /dev/shm/mxl -f db3bd465-2772-484f-8fac-830b0471258b --count 3
```

Example using an MXL URI:

```bash
./mxl-data-probe mxl:///dev/shm/mxl?id=db3bd465-2772-484f-8fac-830b0471258b --count 3
```

Example inspecting two event entries (such as a two-fragment event):

```bash
./mxl-data-probe -d /dev/shm/mxl -f cabbc00d-3860-4438-bc48-8ebdfe67305e --count 2
```

## mxl-gst-testsrc

A binary that uses the gstreamer 'videotestsrc' and 'audiotestsrc' elements to produce video grains and/or audio samples which will be pushed to a MXL Flow. The flow is configured from a NMOS Flow json file. Here's an example of such file :

&emsp; **Note** Don't forget to provide valid description, label and grouphint tag.

```json
{
  "description": "___CHANGE ME___ Long description of the video flow",
  "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ed",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "___CHANGE ME FOR A NAME UNIQUE TO YOUR MEDIA FUNCTION INSTANCE___:Video"
    ]
  },
  "format": "urn:x-nmos:format:video",
  "label": "___CHANGE ME___ Short description of the video flow",
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
&emsp; **Note 1:** The *channel_count* property is a mandatory MXL requirement (not found in standard NMOS definition). To adjust the number of audio channels, simply update the *channel_count* value.

&emsp; **Note 2:** Don't forget to provide valid description, label and grouphint tag.

```json
{
  "description": "___CHANGE ME___ Long description of the audio flow",
  "format": "urn:x-nmos:format:audio",
  "tags": {
    "urn:x-nmos:tag:grouphint/v1.0": [
      "___CHANGE ME FOR A NAME UNIQUE TO YOUR MEDIA FUNCTION INSTANCE___:Audio #1"
    ]
  },
  "label": "___CHANGE ME___ Short description of the audio flow",
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

MXL Source and Sink filters for GStreamer are available [here](https://github.com/dmf-mxl/mxl/blob/main/rust/gst-mxl-rs/README.md).
