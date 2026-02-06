<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Timing Model

As described in the [architecture](./Architecture.md) page, the grains of an MXL flow are organized in a ring buffer. Each index of the ring buffer correspond to a timestamp relative to the PTP epoch as defined by SMPTE 2059-1. MXL does NOT require a PTP/SMPTE 2059 time source : it only _leverages_ the epoch and clock definitions (TAI time) as defined in SMPTE 2059-1.

## Topics

- [Requirements](./Timing-Requirements.md) - Time source requirements for single and multiple host MXL environments
- [Ring buffers](./Timing-Ring-Buffers.md) - Ring buffer organization, indexing logic, and timestamp conversions
- [Real-world timing examples](./Timing-Real-World-Examples.md) - Video frame rates, audio timing calculations, and ring buffer sizing
- [Origin timestamps (OTS)](./Timing-Origin-Timestamps.md) - How origin timestamps work, 2110 receiver examples, and flow replication
- [Audio/Video synchronization](./Timing-AV-Sync.md) - Strategies for synchronizing multiple flows
- [Pacing a write loop](./Timing-Pacing.md) - Video and audio pacing strategies, handling timing drift
- [Time functions](./Timing-Time-Functions.md) - Core time API functions and usage examples
- [Practical considerations](./Timing-Practical-Considerations.md) - Clock selection, late arrivals, ring buffer overrun, and additional resources
