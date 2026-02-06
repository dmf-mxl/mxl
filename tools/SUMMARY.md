# MXL Tools Overview

This directory houses the demonstration applications and utilities for the Media eXchange Layer SDK. Think of these tools as your practical guides to understanding MXL in action. Each tool reveals a different facet of the SDK, from basic flow inspection to sophisticated network transport over RDMA fabrics.

## The Tool Suite

The MXL tools collection includes three primary applications, built and configured through **CMakeLists.txt**:

### mxl-info: Your Flow Management Swiss Army Knife

The inspection and management utility lives in `tools/mxl-info/`. This command-line tool lets you peer into the internal state of MXL flows within a domain. Whether you need to list all flows, examine a specific flow's configuration and runtime state, or clean up inactive flows through garbage collection, mxl-info is your go-to diagnostic tool. It supports both traditional command-line options and modern URI-based addressing.

### mxl-gst: The GStreamer Integration Trio

Found in `tools/mxl-gst/`, this collection demonstrates real-world media integration using GStreamer pipelines. The suite consists of three specialized tools:

- **mxl-gst-testsrc**: Generates synthetic video and audio test patterns (SMPTE bars, color fields, audio tones) and writes them into MXL flows. Perfect for testing and development when you need a reliable, controllable media source.

- **mxl-gst-sink**: The complement to testsrc, this tool reads from MXL flows and plays the media through GStreamer's auto-detection pipelines. It demonstrates zero-copy consumption and handles both video (v210) and audio (float32) flows.

- **mxl-gst-looping-filesrc**: Bridges the gap between file-based media and MXL flows. Point it at any media file (MP4, MPEG-TS, MKV), and it decodes the content, converts it to MXL-compatible formats, and writes it into flows for consumption by other applications.

### mxl-fabrics-demo: Network Transport Over RDMA

Located in `tools/mxl-fabrics-demo/`, this advanced tool demonstrates MXL's network capabilities using libfabric/OFI. It operates in two modes: target (receiver) and initiator (sender), enabling zero-copy RDMA transport of MXL flows across network fabrics. Supports multiple backends including TCP, InfiniBand Verbs, AWS EFA, and shared memory. This tool is conditionally built only when MXL_ENABLE_FABRICS_OFI is enabled.

## Build Configuration

The **CMakeLists.txt** orchestrates the build process. It unconditionally builds mxl-info and mxl-gst, while mxl-fabrics-demo requires explicit enablement through the MXL_ENABLE_FABRICS_OFI option. This conditional build reflects the optional nature of the fabrics transport layer, which introduces additional dependencies on libfabric.

## Common Themes

All tools share a common philosophy: demonstrate a specific MXL feature through practical, real-world usage patterns. They handle TAI timestamp synchronization, support both discrete (video/data) and continuous (audio) flow types, implement robust error handling, and show proper resource lifecycle management. Together, they form a comprehensive tutorial on MXL SDK usage, from simple flow inspection to complex distributed media workflows.
