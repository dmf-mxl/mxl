<!--
SPDX-FileCopyrightText: 2025 2025 Contributors to the Media eXchange Layer project.
SPDX-License-Identifier: Apache-2.0
-->

# GStreamer MXL Tools

This crate implements a GStreamer plugin to allow for reading and writing MXL flows in a GStreamer pipeline.

---

## Table of Contents

- [Overview](#overview)  
- [Usage](#usage)  
- [Example Pipelines](#example-pipelines)  

---

## Overview

The following elements are included:

- **mxlsink**:
    - Creates an MXL flow and writes GStreamer buffers as MXL grains.

- **mxlsrc**:
    - Reads MXL grains out of an MXL flow and outputs GStreamer buffers.

---

## Usage

### mxlsink

| Property  | Description                                                |
| --------- | ---------------------------------------------------------- |
| `flow-id` | UUID of the flow to create.                                |
| `domain`  | Filesystem path to the MXL domain directory.               |

The flow's media type is based on the upstream caps.
`video/x-raw, format=v210` results in a `video/v210` video flow.
`audio/x-raw, format=F32LE` results in an `audio/float32` audio flow.
`meta/x-st-2038, alignment=frame` results in a `video/smpte291` data flow.

### mxlsrc

| Property        | Description                                          |
| --------------- | ---------------------------------------------------- |
| `video-flow-id` | UUID of a video flow to read.                        |
| `audio-flow-id` | UUID of an audio flow to read.                       |
| `data-flow-id`  | UUID of a data flow to read.                         |
| `domain`        | Filesystem path to the MXL domain directory.         |

The src pad's caps are based on the flow's media type.
A `video/v210` flow results in `video/x-raw, format=v210`.
A `audio/float32` flow results in `audio/x-raw, format=F32LE`.
A `video/smpte291` flow results in `meta/x-st-2038, alignment=frame`.

**Note:** Set exactly one of `video-flow-id`, `audio-flow-id`, or `data-flow-id`. The element sets its output caps based on whichever is set.

## Example Pipelines

### Initial setup

The environment variables `LD_LIBRARY_PATH` and `GST_PLUGIN_PATH` must include the location of the `libmxl.so` and `libgstmxl.so`, respectively.

Update the paths below according to your configuration. For example, if the MXL library is built using the preset `Linux-Clang-Release`, and the Rust bindings are built using `--release`:

```sh
export MXL_REPO_ROOT=<path to the root of the MXL repository>
export LD_LIBRARY_PATH="$MXL_REPO_ROOT"/build/Linux-Clang-Release/lib:"$MXL_REPO_ROOT"/build/Linux-Clang-Release/lib/internal
export GST_PLUGIN_PATH="$MXL_REPO_ROOT"/rust/target/release
```

### Verify if the plugin is correctly configured

```sh
gst-inspect-1.0 mxlsrc
gst-inspect-1.0 mxlsink
```

### Define domain and flow IDs


```sh
export MXL_DOMAIN=/dev/shm
export VIDEO_FLOW_ID=2fbec3b1-1b0f-417d-9059-8b94a47197ed
export AUDIO_FLOW_ID=2fbec3b1-1b0f-417d-9059-8b94a47197ee
export DATA_FLOW_ID=2fbec3b1-1b0f-417d-9059-8b94a47197ef
```

### Create video/audio flows from a single file

```sh
export AV_FILE=<path to file with audio and video, e.g. file.mp4>

gst-launch-1.0 filesrc location="$AV_FILE" ! decodebin name=dec ! videoconvert ! queue ! mxlsink flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" dec. ! audioresample ! audioconvert ! queue ! mxlsink flow-id="$AUDIO_FLOW_ID" domain="$MXL_DOMAIN"
```

### Read flows and display video / play audio

**Note**: Make sure the domain and flow IDs are consistent with the producer's.

```sh
gst-launch-1.0 mxlsrc video-flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" ! videoconvert ! queue ! autovideosink mxlsrc audio-flow-id="$AUDIO_FLOW_ID" domain="$MXL_DOMAIN" ! audioconvert ! audioresample ! queue ! autoaudiosink
``` 

**Note**: In the examples above, it is assumed that you are running inside the devcontainer. If not, adjust paths accordingly.

### Create video/audio flows from separate files

```sh
export VIDEO_FILE=<video file path, e.g. file.mp4>
export AUDIO_FILE=<audio file path, e.g. file.mp3>

gst-launch-1.0 filesrc location="$VIDEO_FILE" ! decodebin name=dv ! videoconvert ! queue ! mxlsink flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" filesrc location="$AUDIO_FILE" ! decodebin name=da ! audioresample ! audioconvert ! queue ! mxlsink flow-id="$AUDIO_FLOW_ID" domain="$MXL_DOMAIN"
```


### Create video/audio flows from GStreamer test sources

```sh
gst-launch-1.0 videotestsrc ! timeoverlay valignment=center ! clockoverlay time-format=\"%F %H:%M:%S %Z\" ! video/x-raw,width=1920,height=1080,framerate=25/1,format=v216 ! videoconvert ! queue ! mxlsink flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" audiotestsrc wave=ticks ! audioconvert ! queue ! mxlsink flow-id="$AUDIO_FLOW_ID" domain="$MXL_DOMAIN"
```

### Round-trip CEA-608 closed captions over a data flow

This pair takes plain-text captions from an SRT file, encodes them to CEA-608, wraps them in ST 2038 ANC packets, sends them through MXL as a `video/smpte291` data flow, and decodes them back to plain text on the receiver.

The pipelines below require the `cctost2038anc` / `st2038anctocc` / `tttocea608` / `cea608tott` elements from the `rsclosedcaption` plugin in [`gst-plugins-rs`](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs) (≥ 0.14, which is when the ST 2038 elements were added) and `ccconverter` from `gstreamer1.0-plugins-bad`. The devcontainer builds `rsclosedcaption` from source — see `.devcontainer/Dockerfile`.

Producer:

```sh
export SRT_FILE=<path to an SRT file, e.g. file.srt>

gst-launch-1.0 filesrc location="$SRT_FILE" ! subparse ! tttocea608 mode=pop-on ! ccconverter ! closedcaption/x-cea-608,framerate=30000/1001 ! cctost2038anc ! meta/x-st-2038,alignment=frame,framerate=30000/1001 ! mxlsink flow-id="$DATA_FLOW_ID" domain="$MXL_DOMAIN"
```

Consumer:

```sh
gst-launch-1.0 mxlsrc data-flow-id="$DATA_FLOW_ID" domain="$MXL_DOMAIN" ! meta/x-st-2038,alignment=frame,framerate=30000/1001 ! st2038anctocc ! ccconverter ! closedcaption/x-cea-608 ! cea608tott ! text/x-raw,format=utf8 ! fakesink dump=true
```

For an automated end-to-end smoke test, see `tests/data_round_trip.rs`.
