<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools: How Tools Connect to Each Other

## Simple video pipeline

```
mxl-gst-testsrc                           mxl-gst-sink
┌─────────────────┐                      ┌─────────────────┐
│ videotestsrc    │                      │ v210dec         │
│       ↓         │                      │       ↓         │
│ v210enc         │    MXL Domain        │ videoconvert    │
│       ↓         │  ┌──────────────┐    │       ↓         │
│ mxl writer      │→→│ Video Flow   │→→→→│ autovideosink   │
│                 │  └──────────────┘    │                 │
└─────────────────┘                      └─────────────────┘
```

## A/V synchronized pipeline

```
mxl-gst-testsrc                           mxl-gst-sink
┌─────────────────┐                      ┌─────────────────┐
│ videotestsrc    │  ┌──────────────┐    │ v210dec         │
│       ↓         │→→│ Video Flow   │→→→→│ videoconvert    │
│ v210enc         │  └──────────────┘    │ autovideosink   │
│                 │                      │                 │
│ audiotestsrc    │  ┌──────────────┐    │ autoaudiosink   │
│       ↓         │→→│ Audio Flow   │→→→→│                 │
│ float32         │  └──────────────┘    │                 │
└─────────────────┘                      └─────────────────┘
         ↑                                       ↑
         │                                       │
         └───────── Synchronized by ─────────────┘
                   origin timestamps
```

## Custom GStreamer pipeline

```bash
# Producer
gst-launch-1.0 \
  videotestsrc ! v210enc ! \
  mxlsink domain=/dev/shm/mxl flow-id=<uuid>

# Consumer
gst-launch-1.0 \
  mxlsrc domain=/dev/shm/mxl flow-id=<uuid> ! \
  v210dec ! autovideosink
```

## Additional resources

- **GStreamer documentation:** https://gstreamer.freedesktop.org/documentation/
- **Rust GStreamer plugin:** See `rust/gst-mxl-rs/README.md`
- **Flow configuration:** See [Configuration.md](./Configuration.md) for flow JSON examples
- **Architecture:** See [Architecture.md](./Architecture.md) for flow and grain details

[Back to Tools overview](./Tools.md)
