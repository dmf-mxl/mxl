<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Architecture

The MXL library provides a mechanism for sharing ring buffers of Flow and Grains (see NMOS IS-04 definitions) between media functions executing on the same host (bare-metal or containerized) and/or between media functions executing on hosts connected using fast fabric technologies (RDMA, EFA, etc).

The MXL library provides APIs for zero-overhead grain _sharing_ using a reader/writer model in opposition to a sender/receiver model. With a reader/writer model, no packetization or even memory copy is involved, preserving memory bandwidth and CPU cycles.

## Shared memory model

Flows and Grains are _ring buffers_ stored in memory mapped files written in a _tmpfs_ backed volume or filesystem. The base folder where flows are stored is called an _MXL domain_. Multiple MXL domains can co-exist on the same host.

### Filesystem layout

| Path                                                    | Description                                                                                                                   |
| ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| \${mxlDomain}/                                          | Base directory of the MXL domain                                                                                              |
| \${mxlDomain}/\${flowId}.mxl-flow/                      | Directory containing resources associated with a flow with uuid ${flowId}                                                     |
| \${mxlDomain}/\${flowId}.mxl-flow/data                  | Flow header. contains metadata for a flow ring buffer. Memory mapped by readers and writers.                                  |
| \${mxlDomain}/\${flowId}.mxl-flow/.json                 | NMOS IS-04 Flow resource definition.                                                                                          |
| \${mxlDomain}/\${flowId}.mxl-flow/.access               | File 'touched' by readers (if permissions allow it) to notify flow access. Enables reliable 'lastReadTime' metadata update.   |
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

### Video

- At the moment the only supported video media_type is "video/v210"
- The optional IS-04 (but recommended) "grain_rate" attribute of the NMOS Flow resource is mandatory for video and data flows in MXL.
- The grain_rate in NMOS expresses the frame rate. For interlaced video, users of the API must pass valid frame rates and not field rates. The only valid values for grain_rate for interlaced media is 25/1 or 30000/1001. Internally the SDK will convert it to a field rate (doubled rate)

### Audio

### Ancillary Data

Ancillary data payload is _exactly_ the SMPTE 2110-40 payload format without the RTP headers.
