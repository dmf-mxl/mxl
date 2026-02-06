<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools: mxl-gst-sink

A player that reads from an MXL Flow and displays the video using GStreamer's `autovideosink` and plays audio through `autoaudiosink`.

## When would I use this tool?

- **Validation:** Verify that flows are being written correctly
- **A/V sync testing:** Check synchronization between video and audio flows
- **Latency measurement:** Measure end-to-end pipeline latency
- **Debugging:** Visual confirmation of flow content

## Usage

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

## Examples

**Example 1: Video only**

```bash
./mxl-gst-sink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

**Example 2: Audio only**

```bash
./mxl-gst-sink \
  -d /dev/shm \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

**Example 3: Video and audio together**

```bash
./mxl-gst-sink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

**Example 4: Listen to specific audio channels**

```bash
./mxl-gst-sink \
  -d /dev/shm \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449 \
  -l 2 3  # Listen to channels 2 and 3 (0-indexed)
```

**Example 5: Adjust A/V sync**

```bash
# Delay audio by 50ms to compensate for processing delay
./mxl-gst-sink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449 \
  --av-delay 50000000  # 50ms in nanoseconds
```

**Example 6: Add playback delay**

```bash
# Delay entire playback by 100ms (useful for debugging)
./mxl-gst-sink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449 \
  --playback-delay 100000000  # 100ms
```

## Troubleshooting

**Problem: No video window appears**

Solution: Check GStreamer video sink availability.

```bash
gst-inspect-1.0 autovideosink
```

If missing, install:
```bash
sudo apt-get install gstreamer1.0-plugins-good
```

**Problem: Audio crackling or dropouts**

Solution: Increase read-delay to give more buffering.

```bash
./mxl-gst-sink -d /dev/shm -a <flowId> --read-delay 100000000  # 100ms
```

**Problem: A/V sync issues**

Solution: Use `--av-delay` to adjust relative timing.

```bash
# If audio is early, delay it
./mxl-gst-sink -d /dev/shm -v <videoId> -a <audioId> --av-delay 30000000  # 30ms

# If video is early, delay audio negatively (which delays video)
./mxl-gst-sink -d /dev/shm -v <videoId> -a <audioId> --av-delay -30000000
```

**Problem: "Flow not found"**

Solution: Verify flow exists with mxl-info.

```bash
./mxl-info -d /dev/shm -l
```

[Back to Tools overview](./Tools.md)
