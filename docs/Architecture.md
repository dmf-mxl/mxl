<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture

The MXL library provides a mechanism for sharing ring buffers of Flow and Grains (see NMOS IS-04 definitions) between media functions executing on the same host (bare-metal or containerized) and/or between media functions executing on hosts connected using fast fabric technologies (RDMA, EFA, etc).

The MXL library provides APIs for zero-overhead grain _sharing_ using a reader/writer model in opposition to a sender/receiver model. With a reader/writer model, no packetization or even memory copy is involved, preserving memory bandwidth and CPU cycles.

---

## Core Architecture

- [Shared Memory Model](./Architecture-Shared-Memory.md) - How MXL uses memory-mapped files, filesystem layout, and data model organization
- [Security Model](./Architecture-Security.md) - UNIX permissions, namespaces, and memory mapping security
- [Discrete vs Continuous Flows](./Architecture-Flow-Types.md) - Understanding the two fundamental flow types in MXL

## I/O Models

- [Discrete Grain I/O](./Architecture-Discrete-Grain-IO.md) - Partial grain reads/writes and progressive slicing for video and ancillary data
- [Continuous Ringbuffer I/O](./Architecture-Continuous-Ringbuffer-IO.md) - Sample-level audio I/O with ring buffer geometry and configuration

## Grain Formats

- [video/v210](./Architecture-Format-v210.md) - 10-bit 4:2:2 uncompressed video format
- [video/v210a](./Architecture-Format-v210a.md) - v210 with alpha channel (fill + key)
- [audio/float32](./Architecture-Format-Float32.md) - 32-bit IEEE 754 floating-point audio with de-interleaved channels
- [video/smpte291](./Architecture-Format-SMPTE291.md) - SMPTE ST 291-1 ancillary data packets

## Timing Model

See [Timing Model](./Timing.md) for details on how MXL handles timestamps, frame rates, and synchronization.

## Practical Examples

See [Practical Examples](./Architecture-Practical-Examples.md) for:
- Security and permissions setup
- Docker and Podman deployment
- Kubernetes configuration
- Partial grain I/O examples
- Reading and writing continuous audio samples
- Additional resources and API references
