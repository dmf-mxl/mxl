<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools: GStreamer Filters (Rust gst-mxl-rs)

MXL Source and Sink filters for GStreamer are available in Rust. These provide GStreamer-native integration for MXL flows.

## When would I use these?

- **GStreamer pipelines:** Integrate MXL into existing GStreamer-based media processing
- **Custom pipelines:** Build complex processing graphs with GStreamer's rich plugin ecosystem
- **Rust applications:** Use from Rust code with type-safe bindings

## Installation

```bash
cd rust/gst-mxl-rs
cargo build --release

# Install plugin
sudo cp target/release/libgstmxl.so /usr/lib/x86_64-linux-gnu/gstreamer-1.0/

# Or use in-place for development
export GST_PLUGIN_PATH=$(pwd)/target/release:$GST_PLUGIN_PATH
```

## Usage examples

**Read from MXL and display:**

```bash
gst-launch-1.0 \
  mxlsrc domain=/dev/shm/mxl flow-id=5fbec3b1-1b0f-417d-9059-8b94a47197ed ! \
  v210dec ! \
  videoconvert ! \
  autovideosink
```

**Generate test pattern and write to MXL:**

```bash
gst-launch-1.0 \
  videotestsrc pattern=smpte ! \
  video/x-raw,format=I420,width=1920,height=1080,framerate=30000/1001 ! \
  v210enc ! \
  mxlsink domain=/dev/shm/mxl flow-id=5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

**Audio pipeline:**

```bash
gst-launch-1.0 \
  audiotestsrc freq=440 ! \
  audio/x-raw,format=F32LE,rate=48000,channels=2 ! \
  mxlsink domain=/dev/shm/mxl flow-id=b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

For detailed documentation, see `rust/gst-mxl-rs/README.md`.

[Back to Tools overview](./Tools.md)
