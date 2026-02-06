<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Continuous Ringbuffer I/O

## `mxlContinuousFlowConfigInfo` in context

`mxlFlowReaderGetInfo`, `mxlFlowReaderGetConfigInfo`, `mxlFlowWriterGetInfo`, and `mxlFlowWriterGetConfigInfo` all return a full `mxlFlowInfo` copy
that combines immutable configuration (`mxlFlowConfigInfo`) and mutable runtime (`mxlFlowRuntimeInfo`). For a continuous flow the layout inside the
config union is:

| Field | Type | Meaning |
| --- | --- | --- |
| `channelCount` | `uint32_t` | Number of de-interleaved channel ring buffers allocated for the flow. |
| `bufferLength` | `uint32_t` | Number of sample slots in **each** channel buffer. Readers and writers may only access windows shorter than half of this. |
| `reserved` | `uint8_t[56]` | Zeroed padding so the structure remains 64 bytes wide. |

The `common` block that precedes the `continuous` block contributes additional information that every continuous reader needs:

- `grainRate` carries the sample rate (numerator/denominator rational).
- `format` encodes the payload format (`audio/float32`) so you know the sample word size.
- `maxCommitBatchSizeHint` / `maxSyncBatchSizeHint` advertise how many samples are written in one go. Staying within those hints keeps the reader from spinning on the futex that controls `flow->state.syncCounter`.
- `payloadLocation` and `deviceIndex` tell you if the samples sit in host RAM or device memory.

## Memory layout of continuous flow info

The flow metadata file `${mxlDomain}/${flowId}.mxl-flow/data` contains a single `mxlFlowInfo` object with the following organization:

```
Offset  Size  Description
0x0000  0x04  version (currently 1)
0x0004  0x04  size (must stay 2048)
0x0008  0x80  mxlCommonFlowConfigInfo  (ID, format, rate, batch hints, payload info)
0x0088  0x40  mxlContinuousFlowConfigInfo (channelCount, bufferLength, reserved)
0x00C8  0x40  mxlFlowRuntimeInfo (headIndex, lastWriteTime, lastReadTime, reserved)
0x0108  0x6F8 reserved padding to keep the struct cache-line aligned
```

The actual audio samples do **not** live in `data`. They are stored in `${mxlDomain}/${flowId}.mxl-flow/channels`, a shared memory blob that is
exactly `channelCount * bufferLength * sampleWordSize` bytes long. The layout of this blob is:

```
channel 0 buffer (bufferLength * sampleWordSize bytes)
channel 1 buffer (bufferLength * sampleWordSize bytes)
...
channel N-1 buffer
```

Each channel buffer behaves like a classic circular buffer. The reader and writer API expose this geometry through `mxlWrappedMultiBufferSlice`. The
type inherits the same ideas as the slice documentation referenced above: `base.fragments[0]` and `base.fragments[1]` capture the two contiguous
fragments (slices) you need when a request straddles the wrap-around point, and `stride` spans the gap between channels. When the sample window is
fully contained inside the ring, `fragments[1].size` is simply zero.

## Ring buffer geometry for continuous flows

The ring buffer for continuous flows is designed to prevent race conditions between readers and writers:

```
┌────────────────────────────────────────────────────┐
│           Channel Ring Buffer                      │
│                                                    │
│  Total capacity: bufferLength samples              │
│  Reader window: up to bufferLength/2 samples       │
│  Writer active zone: bufferLength/2 samples        │
└────────────────────────────────────────────────────┘

Example: bufferLength = 4096 samples

    ┌─────────────────────────────────────────────┐
    │           2048 samples                      │  ← Writer active zone
    │        (Writer can write here)              │
    ├─────────────────────────────────────────────┤
    │           2048 samples                      │  ← Reader safe zone
    │       (Reader can read here)                │
    └─────────────────────────────────────────────┘
             ▲
             │
        headIndex (writer position)

As writer advances, reader can safely read up to 2048 samples
back from headIndex without encountering incomplete writes.
```

This half-buffer safety margin ensures that readers never see torn writes from the writer wrapping around.

## Important rules for continuous I/O

- Readers can only observe up to `bufferLength / 2` samples in one call. The other half of the buffer is considered "write in progress", which prevents races when a writer is wrapping around.
- `index` is inclusive: asking for `(headIndex, 256)` returns samples `headIndex - 255` through `headIndex`.
- Never assume the sample window is contiguous. Instead always treat the window as two contiguous slices, that may or may not be empty (i.e. have a `size` of `0`).
- Continuous flows ride on the same slice/fragment helpers as discrete flows. `fragments[0]` and `fragments[1]` are slices of bytes, and `stride`
  identifies how far you have to slide (hence "stride") to reach the same sample in the next channel.

For working code examples, see the [Practical Examples](./Architecture-Practical-Examples.md) document.

![Continuous Flow Memory Layout](./assets/continuous-flow-memory-layout.png)

---

[Back to Architecture overview](./Architecture.md)
