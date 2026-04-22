<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Ring Buffer Types

The MXL SDK provides two ring buffer types: **Discrete** and **Continuous**.

- **Discrete ring buffers** are used for granular data types such as video and ancillary data.
- **Continuous ring buffers** are used for audio.

These ring buffer types are central to how MXL manages media data in memory, optimizing for both performance and flexibility.

## Discrete Ring Buffers

Discrete ring buffers are designed to handle data that is naturally segmented into distinct units, such as video frames or ancillary data packets. Each unit, or "grain," is stored in a slot in the ring buffer. This allows for efficient access, writing, and reading of individual grains without interfering with other data.

**Key characteristics:**

- Each grain is independently accessible.
- Supports partial writes (e.g., writing a grain in slices).
- Suitable for video and ancillary data.

## Continuous Ring Buffers

Continuous ring buffers are optimized for data streams that are best represented as a continuous flow, such as audio samples. Instead of discrete units, the buffer is filled with a stream of samples, and readers/writers operate on windows of data.

**Key characteristics:**

- Data is accessed as a continuous window.
- Supports efficient, low-latency access for real-time audio.
- Suitable for audio data.

!!! success
    By providing both discrete and continuous ring buffer types, MXL can efficiently handle a wide variety of media data, ensuring high performance and flexibility for different use cases in media processing pipelines.
