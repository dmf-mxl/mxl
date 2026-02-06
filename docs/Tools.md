<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools

MXL provides a suite of command-line tools for inspecting, generating, and consuming flows. This document describes each tool, when to use it, and how to troubleshoot common issues.

## Contents

1. [mxl-info](./Tools-mxl-info.md) - Diagnostic tool for inspecting domains and flows, including usage, examples, and troubleshooting
2. [mxl-gst-testsrc](./Tools-mxl-gst-testsrc.md) - Test signal generator for video and audio, including usage, flow definitions, examples, and troubleshooting
3. [mxl-gst-sink](./Tools-mxl-gst-sink.md) - Player for MXL flows, including usage, examples, and troubleshooting
4. [mxl-gst-looping-filesrc](./Tools-mxl-gst-looping-filesrc.md) - Utility for looping MPEG-TS files into MXL flows
5. [GStreamer Filters (Rust gst-mxl-rs)](./Tools-GStreamer-Filters.md) - MXL source and sink filters for GStreamer pipelines
6. [How Tools Connect to Each Other](./Tools-Pipeline-Connections.md) - Pipeline diagrams and connection examples

## Additional resources

- **GStreamer documentation:** https://gstreamer.freedesktop.org/documentation/
- **Rust GStreamer plugin:** See `rust/gst-mxl-rs/README.md`
- **Flow configuration:** See [Configuration.md](./Configuration.md) for flow JSON examples
- **Architecture:** See [Architecture.md](./Architecture.md) for flow and grain details
