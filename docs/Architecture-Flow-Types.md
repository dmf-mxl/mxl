<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Discrete vs Continuous Flows

MXL supports two fundamentally different flow types:

## Discrete flows

Used for video frames and ancillary data. Each grain represents a complete unit of media (a frame, a VBI packet, etc).

**Characteristics:**

- Fixed grain size determined by format and resolution
- Grains are indexed by frame number
- Each grain is a separate memory-mapped file
- Writers commit grains atomically or progressively (slices)
- Readers block until a complete or partial grain is available

**Use cases:**

- Video: v210, v210a
- Ancillary data: SMPTE ST 291

## Continuous flows

Used for audio streams. Instead of discrete grains, continuous flows expose a sample-level ring buffer.

**Characteristics:**

- Multiple de-interleaved channel buffers
- Indexed by absolute sample number (not frame number)
- Single `channels` file contains all channels
- Writers commit batches of samples
- Readers request sample windows by index and count

**Use cases:**

- Audio: float32 multi-channel

The continuous flow model avoids the overhead of frame boundaries for audio, allowing arbitrary read/write batch sizes that don't need to align to video frame boundaries.

---

[Back to Architecture overview](./Architecture.md)
