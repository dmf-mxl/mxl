<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture

The MXL library provides a mechanism for sharing ring buffers of Flow and Grains (see NMOS IS-04 definitions) between media functions executing on the same host (bare-metal or containerized) and/or between media functions executing on hosts connected using fast fabric technologies (RDMA, EFA, etc).

The MXL library provides APIs for zero-overhead grain _sharing_ using a reader/writer model in opposition to a sender/receiver model. With a reader/writer model, no packetization or even memory copy is involved, preserving memory bandwidth and CPU cycles.

## Shared memory model

Flows and Grains are _ring buffers_ stored in memory mapped files written in a _tmpfs_ backed volume or filesystem. The base folder where flows are stored is called an _MXL domain_. Multiple MXL domains can co-exist on the same host.

### Filesystem layout

| Path                                                    | Description                                                                                                                   |
|---------------------------------------------------------| ----------------------------------------------------------------------------------------------------------------------------- |
| \${mxlDomain}/                                          | Base directory of the MXL domain                                                                                              |
| \${mxlDomain}/\${flowId}.mxl-flow/                      | Directory containing resources associated with a flow with uuid ${flowId}                                                     |
| \${mxlDomain}/\${flowId}.mxl-flow/data                  | Flow header. contains metadata for a flow ring buffer. Memory mapped by readers and writers.                                  |
| \${mxlDomain}/\${flowId}.mxl-flow/flow_def.json         | NMOS IS-04 Flow resource definition.                                                                                          |
| \${mxlDomain}/\${flowId}.mxl-flow/access                | File 'touched' by readers (if permissions allow it) to notify flow access. Enables reliable 'lastReadTime' metadata update.   |
| \${mxlDomain}/\${flowId}.mxl-flow/grains/               | Directory where individual grains are stored.                                                                                 |
| \${mxlDomain}/\${flowId}.mxl-flow/grains/\${grainIndex} | Grain Header and optional payload (if payload is in host memory and not device memory ). Memory mapped by readers and writers |

### Note

- FlowWriters will obtain a SHARED advisory lock on any memory mapped files (data and grains) and hold it until closed. This is used to detect stale flows in the _mxlGarbageCollectFlows()_ function (for example, when a crashed media function failed to release the flow properly)

## Security model

### UNIX permissions

The MXL SDK uses UNIX files and directories, allowing it to leverage operating system file permissions (users, groups, etc) not only at the mxl domain level but also at the individual flow level.

### IPC and Process namespaces

The memory mapping model used by MXL does not require a shared IPC or process namespace, making it suitable for safe use in containerized environments.

### Memory Mapping

An _mxlFlowReader_ will only _mmap_ flow resources in readonly mode (PROT_READ), allowing readers to access flows stored in a readonly volume or filesystem. In order to support this use case, synchronization between readers and writers is performed using futexes and not using POSIX mutexes, which would require write access to the mutex stored in shared memory.

## Timing model

See [timing model](./timing.md)

## Grain I/O

Grain I/O can be 'partial'. In other words, a FlowWriter can write the bytes of a grain progressively (as slices for example).

The following example demonstrates how a FlowWriter may write a grain as slices using multiple `mxlFlowWriterCommitGrain()` calls. The important field here is the mxlGrainInfo.committedSize: it tells us how many bytes in the grain are valid. A complete grain will have `mxlGrainInfo.committedSize == mxlGrainInfo.grainSize`. It is imperative that FlowReaders _always_ take into consideration the `mxlGrainInfo.committedSize` field before making use of the grain buffer. The `mxlFlowReaderGetGrain` function will return as soon as new data is committed to the grain.

### Example Grain I/O (from the [unit tests](../lib/tests/test_flows.cpp))

```c++
    ///
    /// Write and read a grain in 8 slices
    ///

    /// Open the grain.
    mxlGrainInfo gInfo;
    uint8_t *buffer = nullptr;

    /// Open the grain for writing.
    REQUIRE( mxlFlowWriterOpenGrain( instanceWriter, writer, index, &gInfo, &buffer ) == MXL_STATUS_OK );

    const size_t maxSlice = 8;
    auto sliceSize = gInfo.grainSize / maxSlice;
    for ( size_t slice = 0; slice < maxSlice; slice++ )
    {
        /// Write a slice the grain.
        gInfo.committedSize += sliceSize;
        REQUIRE( mxlFlowWriterCommitGrain( instanceWriter, writer, &gInfo ) == MXL_STATUS_OK );

        mxlFlowInfo sliceFlowInfo;
        REQUIRE( mxlFlowReaderGetInfo( instanceReader, reader, &sliceFlowInfo ) == MXL_STATUS_OK );
        REQUIRE( sliceFlowInfo.headIndex == index );

        /// Read back the partial grain using the flow reader.
        REQUIRE( mxlFlowReaderGetGrain( instanceReader, reader, index, 8, &gInfo, &buffer ) == MXL_STATUS_OK );

        // Validate the committed size
        REQUIRE( gInfo.committedSize == sliceSize * ( slice + 1 ) );
    }
```

# Grain formats

## Video

Video grains can be of two different formats: video/v210 for video without transparency and video/v210+alpha for fill and key signals (video with alpha transparency).

### video/v210

The v210 format is an uncompressed buffer format carrying 10 bit 4:2:2 video. A detailed description of the format can be found [here](https://wiki.multimedia.cx/index.php/V210) and [here](https://developer.apple.com/library/archive/technotes/tn2162/_index.html#//apple_ref/doc/uid/DTS40013070-CH1-TNTAG8-V210__4_2_2_COMPRESSION_TYPE).

#### video/v210alpha

This format contains both fill and key inside a single grain.  The fill part starts at byte 0 of the grain and follows the v210 definition above. The key buffer is found immediately after the fill buffer.  Samples are organized in blocks of 32 bit values in little-endian.  Each block contains 3 luma samples, one each in bits 0 - 9, 10 - 19 and 20 - 29, the remaining two bits are unused.  The start of each line is aligned to a multiple of 4 bytes, where unused blocks are padded with 0.  The last block of a line might have more padding than just the last 2 bits if the width is not divisible by 3.  For example, 1280x720 resolution has padding for bits 20 to 31 on the last block.

Alpha samples are 'straight' (not premultiplied) and follow the same data range as the fill.  For example, if the fill is _video range_ (64 to 960) then the samples of the key are also _video range_.

```
Video/v210alpha Grain Structure:
┌────────────────────────────────────────────────────────────────┐
│                         FILL BUFFER                            │
│                        (v210 format)                           │
├────────────────────────────────────────────────────────────────┤
│                         KEY BUFFER                             │
│                       (alpha channel)                          │
└────────────────────────────────────────────────────────────────┘

32-bit Block Structure (Little-Endian):
┌───────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ 31 30 │ 29 28 27 26 25 24 23 22 21 20 │ 19 18 17 16 15 14 13 12 11 10 │  9  8  7  6  5  4  3  2  1  0 │
├───────┼───────────────────────────────┼───────────────────────────────┼───────────────────────────────┤
│unused │         Luma Sample 2         │         Luma Sample 1         │         Luma Sample 0         │
│  bits │         (bits 20-29)          │         (bits 10-19)          │           (bits 0-9)          │
└───────┴───────────────────────────────┴───────────────────────────────┴───────────────────────────────┘

Line Structure:
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│ Block 0 │ Block 1 │ Block 2 │   ...   │ Block N │ Padding │
│ 3 luma  │ 3 luma  │ 3 luma  │         │ 1-3 luma│ (zeros) │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
                                                            ↑
                                                      4-byte aligned

Example (1280x720):
- Width: 1280 pixels
- Blocks per line: ⌈1280/3⌉ = 427 blocks
- Last block: 1 pixel luma sample + zero padding in bits 10-31


Key points:
• Fill buffer (v210) comes first, followed immediately by key buffer (alpha)
• Each 32-bit block contains 3 luma samples (10 bits each)
• Lines are 4-byte aligned with zero padding
```

## Audio

### audio/float32

Audio is stored as 32 bit IEEE754 float values in the range of -1.0 to +1.0.

### Ancillary Data

Ancillary data payload is based on [RFC 8331](https://datatracker.ietf.org/doc/html/rfc8331#section-2).   Only the bytes starting at the *Length* field (See section 2 of RFC 8331) are stored in the grain. (the bytes 0 to 13 are redundant in the context of MXL and are not stored).
