<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools

## mxl-info

Simple tool that uses the MXL SDK to open a flow by ID and prints the flow details.

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

## mxl-data-probe

A tool that opens an MXL ancillary Data flow (`media_type: video/smpte291`), reads one or more grains starting at the current head index, and prints the RFC-8331 ANC elements found in each grain.

```bash
./mxl-data-probe [OPTIONS] [ADDRESS...]


POSITIONALS:
  ADDRESS TEXT ...            MXL URI

OPTIONS:
  -h,     --help              Print this help message and exit
          --version           Display program version information and exit
  -d,     --domain TEXT:DIR   The MXL domain directory
  -f,     --flow TEXT         The flow id to read
  -c,     --count UINT:UINT in [1 - 18446744073709551615] 
                              Number of grains to read from the current head index
  -t,     --timeout-ms UINT   Timeout per grain read in milliseconds

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

## mxl-gst-testsrc

A binary that uses the GStreamer `videotestsrc` and `audiotestsrc` elements to produce video grains and/or audio samples which will be pushed to an MXL Flow.

The GStreamer pipeline ends at an `appsink`. This program pulls each sample from that sink and writes it with the MXL SDK. The flow is configured from a NMOS Flow JSON file.

Here's an example of such file.

> [!NOTE]
> Don't forget to provide valid description, label, and group-hint tag.

`video_flow.json`

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

> [!NOTE]
> 1. The `channel_count` property is a mandatory MXL requirement (not found in the NMOS audio Flow schema).
>    To adjust the number of audio channels, simply update the `channel_count` value.
> 2. Don't forget to provide valid description, label and group-hint tag.

`audio_flow.json`

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

`video_options.json`

```json
{
  "maxCommitBatchSizeHint": 60,
  "maxSyncBatchSizeHint": 60
}
```

`audio_options.json`

```json
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

The same test pattern and tones are written to MXL flows using the `mxlsink` element in [Equivalent to mxl-gst-testsrc](#equivalent-to-mxl-gst-testsrc).

## mxl-gst-sink

A binary that reads from an MXL Flow and displays the flow using the GStreamer element `autovideosink`. This program reads grains with the MXL SDK and pushes each buffer into an `appsrc`.

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
          --pts-offset UINT   The time in nanoseconds by which to delay playback of grains
                              relative to their MXL-based timestamps. This sets a fixed value
                              that will not change during runtime. If the parameter is not
                              provided, the PTS will be automatically increased so that no PTS
                              value supplied to GStreamer is ever in the past.
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

The same flows are played with the `mxlsrc` element in [Equivalent to mxl-gst-sink](#equivalent-to-mxl-gst-sink).

## mxl-gst-looping-filesrc

A utility that continuously loops through MPEG-TS files to generate video grains to push into MXL flows. The GStreamer pipeline ends at an `appsink` for video and an `appsink` for audio. This program pulls each sample (buffer) and writes it with the MXL SDK.

#### Prerequisites

This application requires the `looping_filesrc` GStreamer plugin located in `utils/gst-looping-filesrc/`.  After building the plugin successfully, set the `GST_PLUGIN_PATH` environment variable to enable GStreamer to locate and load the plugin.

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
          --video-id TEXT:UUID 
                              The MXL video flow id
          --audio-id TEXT:UUID 
                              The MXL audio flow id
```

`looping_filesrc` reads an MPEG-TS file and effectively rewinds it to the start every time it reaches the end. It cannot guarantee seamless playback, because the MPEG-TS timestamps do not meet at the rewind.

The pipelines below each create one file. Each file is 300 frames at 30000/1001, which is 10.01 seconds: ten closed GOPs of 30 frames, and 1001 Opus frames of 10 ms (480480 samples at 48 kHz). The picture in the file is H.265 main profile, 8-bit 4:2:0. Playback converts it to `v210` at 1920×1080, 30000/1001, matching `lib/tests/data/v210_flow.json`. The audio decodes to 48 kHz stereo `F32LE`, matching `lib/tests/data/audio_flow.json`.

The same files are written to MXL flows using the `mxlsink` element in [Equivalent to mxl-gst-looping-filesrc](#equivalent-to-mxl-gst-looping-filesrc).

#### Test pattern

The same bars, label, and tones as `mxl-gst-testsrc`. A 100 Hz tone is on the first channel and a 200 Hz tone is on the second. `timeoverlay` draws each buffer's timestamp on the bars, so the rewind is visible.

```bash
gst-launch-1.0 -e \
  videotestsrc is-live=false num-buffers=300 pattern=smpte ! \
  video/x-raw,format=I420,width=1920,height=1080,framerate=30000/1001,colorimetry=bt709 ! \
  textoverlay text="EBU DMF MXL" font-desc="Sans, 36" ! \
  timeoverlay ! \
  x265enc key-int-max=30 speed-preset=ultrafast tune=zerolatency bitrate=8000 \
    option-string="keyint=30:min-keyint=30:scenecut=0:open-gop=0:bframes=0" ! \
  h265parse ! queue ! mux. \
  audiotestsrc is-live=false num-buffers=1001 samplesperbuffer=480 freq=100 ! \
  'audio/x-raw,format=S16LE,rate=48000,channels=1,channel-mask=(bitmask)0x1' ! queue ! m.sink_0 \
  audiotestsrc is-live=false num-buffers=1001 samplesperbuffer=480 freq=200 ! \
  'audio/x-raw,format=S16LE,rate=48000,channels=1,channel-mask=(bitmask)0x2' ! queue ! m.sink_1 \
  interleave name=m ! \
  audioconvert ! audio/x-raw,format=S16LE,rate=48000,channels=2 ! \
  opusenc frame-size=10 ! queue ! mux. \
  mpegtsmux name=mux ! filesink location=mxl-gst-testsrc.ts
```

#### A/V sync

`avsyncvideotestsrc` and `avsyncaudiotestsrc` come from `libgstavsynctest.so`, in the same `GST_PLUGIN_PATH` directory as `libgstmxl.so`. The default pip interval is 1 second on both. The bar is at the centre of the frame on each pip, and the tone is centred on that same instant. Set `pip-interval` to the same value on each source if you change it. The file stores the sweeping bar and the tone pip. It does not store the CEA-708 caption or the frame-index ancillary.

```bash
gst-launch-1.0 -e \
  avsyncvideotestsrc is-live=false num-buffers=300 ! \
  video/x-raw,format=v210,width=1920,height=1080,framerate=30000/1001 ! \
  videoconvert ! video/x-raw,format=I420 ! \
  x265enc key-int-max=30 speed-preset=ultrafast tune=zerolatency bitrate=8000 \
    option-string="keyint=30:min-keyint=30:scenecut=0:open-gop=0:bframes=0" ! \
  h265parse ! queue ! mux. \
  avsyncaudiotestsrc is-live=false num-buffers=1001 samples-per-buffer=480 ! \
  audio/x-raw,format=F32LE,rate=48000,channels=2 ! \
  audioconvert ! audio/x-raw,format=S16LE,rate=48000,channels=2 ! \
  opusenc frame-size=10 ! queue ! mux. \
  mpegtsmux name=mux ! filesink location=avsynctest.ts
```

## GStreamer plugin

The `mxl` plugin (`libgstmxl.so`) provides two elements, `mxlsink` and `mxlsrc`. `mxlsink` writes GStreamer buffers into an MXL Flow. `mxlsrc` reads an MXL Flow into GStreamer buffers. Each element calls the MXL SDK itself, so the example pipelines below can be run with `gst-launch-1.0`. They provide the functionality of `mxl-gst-testsrc`, `mxl-gst-sink`, and `mxl-gst-looping-filesrc` with no application code.

The GStreamer plugin is documented [here](../rust/gst-mxl-rs/README.md). Set `GST_PLUGIN_PATH` and `LD_LIBRARY_PATH` as that README describes before running the examples below. The `looping_filesrc` example also needs that plugin on `GST_PLUGIN_PATH`, as described under [mxl-gst-looping-filesrc](#mxl-gst-looping-filesrc).

### Equivalent to mxl-gst-testsrc

Video only:

```bash
gst-launch-1.0 \
  videotestsrc is-live=true do-timestamp=true pattern=smpte ! \
  textoverlay text="EBU DMF MXL" font-desc="Sans, 36" ! \
  clockoverlay ! \
  videoconvert ! \
  video/x-raw,format=v210,width=1920,height=1080,framerate=30000/1001 ! \
  queue ! \
  mxlsink flow-id=5fbec3b1-1b0f-417d-9059-8b94a47197ed domain=/dev/shm
```

Audio only:

```bash
gst-launch-1.0 \
  audiotestsrc is-live=true do-timestamp=true freq=100 ! \
  'audio/x-raw,channels=1,channel-mask=(bitmask)0x1' ! queue ! m.sink_0 \
  audiotestsrc is-live=true do-timestamp=true freq=200 ! \
  'audio/x-raw,channels=1,channel-mask=(bitmask)0x2' ! queue ! m.sink_1 \
  interleave name=m ! \
  audioconvert ! \
  audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved ! \
  queue ! \
  mxlsink flow-id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449 domain=/dev/shm
```

Video and audio:

```bash
gst-launch-1.0 \
  videotestsrc is-live=true do-timestamp=true pattern=smpte ! \
  textoverlay text="EBU DMF MXL" font-desc="Sans, 36" ! \
  clockoverlay ! \
  videoconvert ! \
  video/x-raw,format=v210,width=1920,height=1080,framerate=30000/1001 ! \
  queue ! \
  mxlsink flow-id=5fbec3b1-1b0f-417d-9059-8b94a47197ed domain=/dev/shm \
  audiotestsrc is-live=true do-timestamp=true freq=100 ! \
  'audio/x-raw,channels=1,channel-mask=(bitmask)0x1' ! queue ! m.sink_0 \
  audiotestsrc is-live=true do-timestamp=true freq=200 ! \
  'audio/x-raw,channels=1,channel-mask=(bitmask)0x2' ! queue ! m.sink_1 \
  interleave name=m ! \
  audioconvert ! \
  audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved ! \
  queue ! \
  mxlsink flow-id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449 domain=/dev/shm
```

### Equivalent to mxl-gst-sink

Video only:

```bash
gst-launch-1.0 \
  mxlsrc video-flow-id=5fbec3b1-1b0f-417d-9059-8b94a47197ed domain=/dev/shm ! \
  videoconvert ! \
  videoscale ! \
  queue ! \
  autovideosink
```

Audio only:

```bash
gst-launch-1.0 \
  mxlsrc audio-flow-id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449 domain=/dev/shm ! \
  audioconvert ! \
  queue ! \
  autoaudiosink
```

Video and audio:

```bash
gst-launch-1.0 \
  mxlsrc video-flow-id=5fbec3b1-1b0f-417d-9059-8b94a47197ed domain=/dev/shm ! \
  videoconvert ! \
  videoscale ! \
  queue ! \
  autovideosink \
  mxlsrc audio-flow-id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449 domain=/dev/shm ! \
  audioconvert ! \
  queue ! \
  autoaudiosink
```

### Equivalent to mxl-gst-looping-filesrc

The MPEG-TS files are made under [mxl-gst-looping-filesrc](#mxl-gst-looping-filesrc). `decodebin` uses `nvh265dec` when GStreamer's NVDEC H.265 decoder is available, and a software H.265 decoder otherwise.

```bash
export MEDIA_FILE=mxl-gst-testsrc.ts
# DeepStream's nvv4l2decoder outputs memory:NVMM, which videoconvert cannot accept.
export GST_PLUGIN_FEATURE_RANK=nvv4l2decoder:NONE

gst-launch-1.0 \
  looping_filesrc location="$MEDIA_FILE" ! decodebin name=dec ! \
  queue ! videorate ! videoconvert ! \
  video/x-raw,format=v210,colorimetry=bt709 ! \
  queue ! \
  mxlsink flow-id=5fbec3b1-1b0f-417d-9059-8b94a47197ed domain=/dev/shm \
  dec. ! queue ! audioconvert ! \
  audio/x-raw,format=F32LE,rate=48000,layout=interleaved ! \
  queue ! \
  mxlsink flow-id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449 domain=/dev/shm
```
