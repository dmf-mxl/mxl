# <!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
# <!-- SPDX-License-Identifier: CC-BY-4.0 -->
#
# Using the MXL C Public API

This document provides comprehensive examples for integrating MXL into your media processing application.

## Overview

MXL uses a shared-memory architecture for zero-copy media exchange between processes. All operations require an instance handle representing your connection to an MXL domain. Flow writers produce media data, and flow readers consume it. MXL supports discrete flows (video, ancillary data) and continuous flows (audio).

The examples below cover both the C API and Rust bindings, demonstrating video, audio, and ancillary data handling with complete error checking patterns.

## Getting Started

- [Basic Setup](./Usage-Basic-Setup.md) - Creating instances, writers, readers, and releasing resources

## Writing Media

- [Video Grain Writing (v210)](./Usage-Video-Grain-Writing.md) - Complete v210 video grain writing example with timestamps and progressive/sliced writes
- [Video Grain with Alpha (v210a)](./Usage-Video-Alpha.md) - Writing v210a grains with fill and key buffers
- [Audio Sample Writing (float32)](./Usage-Audio-Writing.md) - Multi-channel audio writing for continuous flows
- [Ancillary Data Writing (SMPTE ST 291)](./Usage-Ancillary-Data.md) - CEA-608 and other ANC packet formats

## Reading Media

- [Reading Grains](./Usage-Reading-Grains.md) - Reading discrete video and data grains with blocking and polling patterns
- [Reading Audio Samples](./Usage-Reading-Audio.md) - Reading continuous audio flows with ring buffer handling

## Advanced Topics

- [Synchronization Groups](./Usage-Sync-Groups.md) - Aligning multiple flows (video + audio) to the same timestamps
- [Error Handling Best Practices](./Usage-Error-Handling.md) - Robust error checking patterns and common status codes

## Language Bindings

- [Rust API Examples](./Usage-Rust-API.md) - Idiomatic Rust bindings with RAII and type safety

## Reference

- [Flow Definition JSON Examples](./Usage-Flow-Definition-JSON.md) - NMOS IS-04 flow definitions for all supported formats

## Additional Resources

- **C API reference**: See `lib/include/mxl/SUMMARY.md` for complete API documentation
- **Rust API reference**: See `rust/mxl-rs/README.md` for Rust bindings
- **Architecture details**: See [Architecture.md](./Architecture.md) for shared memory model and format details
- **Timing model**: See [Timing.md](./Timing.md) for timestamp calculations and synchronization
- **Configuration**: See [Configuration.md](./Configuration.md) for domain and flow options
- **Code examples**: See `lib/tests/` for comprehensive unit tests demonstrating all APIs
