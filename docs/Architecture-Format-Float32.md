<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Grain Format: audio/float32

The `audio/float32` format has audio stored as 32 bit [IEEE 754](https://standards.ieee.org/ieee/754/6210/) float values with a full-scale range of \[−1.0 ; +1.0]\. This is the same audio representation as in RIFF/WAV files with `<wFormatTag>` `0x0003` `WAVE_FORMAT_IEEE_FLOAT`.

Please note that flow producing media functions are not required to stay within the full-scale range and *should not* artificially clamp values to that range. Instead flow consuming media functions that are sensitive to levels exceeding 0 dbFS should, as a fail-safe measure clamp the sample values read to the supported range. This gives operators increased freedom in architecting their processing pipelines and retaining maximum fidelity.

**Format details:**

- Sample size: 4 bytes (32 bits)
- Encoding: IEEE 754 single-precision floating-point
- Nominal range: -1.0 to +1.0 (0 dBFS)
- Actual range: Any valid float32 value (implementers may exceed nominal range)
- Channel layout: De-interleaved (planar). Each channel has its own ring buffer.

**Channel layout:**

For an 8-channel flow with bufferLength = 4096:

```
┌─────────────────────────────────────────────┐
│  Channel 0: 4096 float32 samples            │ (16384 bytes)
├─────────────────────────────────────────────┤
│  Channel 1: 4096 float32 samples            │
├─────────────────────────────────────────────┤
│  Channel 2: 4096 float32 samples            │
├─────────────────────────────────────────────┤
│        ...  (channels 3-6)                  │
├─────────────────────────────────────────────┤
│  Channel 7: 4096 float32 samples            │
└─────────────────────────────────────────────┘

Total: 8 × 4096 × 4 = 131072 bytes
```

This de-interleaved layout improves cache performance for channel-wise processing and avoids read-modify-write cycles when updating individual channels.

**Ring buffer geometry:**

See the [Continuous Ringbuffer I/O](./Architecture-Continuous-Ringbuffer-IO.md) section for details on how the circular buffer wrapping works.

---

[Back to Architecture overview](./Architecture.md)
