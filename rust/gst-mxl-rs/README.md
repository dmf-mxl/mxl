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
    - Create an MXL flows and writes GStreamer buffers as MXL grains.

- **mxlsrc**:
    - Reads MXL grains out of an MXL flow and outputs GStreamer buffers.

---

## Usage

### mxlsink

mxlsink requires the following arguments:

> **flow-id**: The ID of the flow that will be created.

> **domain**: The path to the directory where the domain lives.

### mxlsrc

mxlsrc requires the following arguments:

> **video-id**: ID of the video flow that will be read.

> **audio-id**: ID of the audio flow that will be read.

> **domain**: The path to the directory where the domain lives.

**Note:** One mxlsrc will only accept either a video-id or audio-id, not both.

**Note:** Obvious question: Why not use only "flow-id" and detect whether it is video, audio or ANC? That will be done later.

## Example Pipelines

### Initial setup

The environment variables `LD_LIBRARY_PATH` and `GST_PLUGIN_PATH` must include the location of the `libmxl.so` and `libgstmxl.so`, respectively.

Update the paths below according to your configuration. For example, if the MXL library is built using the preset `Linux-Clang-Release`, and the Rust bindings are built using `--release`:

```
export MXL_REPO_ROOT=<path to the root of the MXL repository>
export LD_LIBRARY_PATH="$MXL_REPO_ROOT"/build/Linux-Clang-Release/lib:"$MXL_REPO_ROOT"/build/Linux-Clang-Release/lib/internal
export GST_PLUGIN_PATH="$MXL_REPO_ROOT"/rust/target/release
```

### Verify if the plugin is correctly configured

```
gst-inspect-1.0 mxlsrc
gst-inspect-1.0 mxlsink
```

### Define domain and flow IDs


```
export MXL_DOMAIN=/dev/shm
export VIDEO_FLOW_ID=2fbec3b1-1b0f-417d-9059-8b94a47197ed
export AUDIO_FLOW_ID=2fbec3b1-1b0f-417d-9059-8b94a47197ee
```

### Create video/audio flows from a single file

```
export AV_FILE=<path to file with audio and video, e.g. file.mp4>

gst-launch-1.0 filesrc location="$AV_FILE" ! decodebin name=dec ! videoconvert ! queue ! mxlsink flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" dec. ! audioresample ! audioconvert ! queue ! mxlsink flow-id="$AUDIO_FLOW_ID" domain="$MXL_DOMAIN"
```

### Read flows and display video / play audio

**Note**: Make sure the domain and flow IDs are consistent with the producer's.

```
gst-launch-1.0 mxlsrc video-flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" ! videoconvert ! queue ! autovideosink mxlsrc audio-flow-id="$AUDIO_FLOW_ID" domain="$MXL_DOMAIN" ! audioconvert ! audioresample ! queue ! autoaudiosink
``` 

**Note**: In the examples above, it is assumed that you are running inside the devcontainer. If not, adjust paths accordingly.

### Create video/audio flows from separate files

```
export VIDEO_FILE=<video file path, e.g. file.mp4>
export AUDIO_FILE=<audio file path, e.g. file.mp3>

gst-launch-1.0 filesrc location="$VIDEO_FILE" ! decodebin name=dv ! videoconvert ! queue ! mxlsink flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" filesrc location="$AUDIO_FILE" ! decodebin name=da ! audioresample ! audioconvert ! queue ! mxlsink flow-id="$AUDIO_FLOW_ID" domain="$MXL_DOMAIN"
```


### Create video/audio flows from GStreamer test sources

```
gst-launch-1.0 videotestsrc ! timeoverlay valignment=center ! clockoverlay time-format=\"%F %H:%M:%S %Z\" ! video/x-raw,width=1920,height=1080,framerate=25/1,format=v216 ! videoconvert ! queue ! mxlsink flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" audiotestsrc wave=ticks ! audioconvert ! queue ! mxlsink flow-id="$AUDIO_FLOW_ID" domain="$MXL_DOMAIN"
```
