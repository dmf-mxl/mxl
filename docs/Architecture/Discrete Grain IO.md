<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Discrete Grain I/O

Grain I/O can be 'partial'. In other words, a FlowWriter can write the bytes of a grain progressively (as slices for example).

The following example demonstrates how a FlowWriter may write a grain as slices using multiple `mxlFlowWriterCommitGrain()` calls. The important field here is the `mxlGrainInfo.committedSize`: it tells us how many bytes in the grain are valid. A complete grain will have `mxlGrainInfo.committedSize == mxlGrainInfo.grainSize`. It is imperative that FlowReaders _always_ take into consideration the `mxlGrainInfo.committedSize` field before making use of the grain buffer. The `mxlFlowReaderGetGrain` function will return as soon as new data is committed to the grain.

## Example Grain I/O 

This is taken from the [unit tests](https://github.com/dmf-mxl/mxl/blob/main/lib/tests/test_flows.cpp)

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

    mxlFlowRuntimeInfo sliceFlowRuntimeInfo;
    REQUIRE( mxlFlowReaderGetRuntimeInfo( instanceReader, reader, &sliceFlowRuntimeInfo ) == MXL_STATUS_OK );
    REQUIRE( sliceFlowRuntimeInfo.headIndex == index );

    /// Read back the partial grain using the flow reader.
    REQUIRE( mxlFlowReaderGetGrain( instanceReader, reader, index, 8, &gInfo, &buffer ) == MXL_STATUS_OK );

    // Validate the committed size
    REQUIRE( gInfo.committedSize == sliceSize * ( slice + 1 ) );
}
```

## Key Points

- Grains can be written in slices, not just as a single block.
- The committed size must always be checked by readers.
- Partial grains are visible to readers as soon as they are committed.
