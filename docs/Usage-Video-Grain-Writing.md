<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Video Grain Writing (v210)

This example demonstrates writing a 1920x1080 progressive v210 video grain.

## Complete example

```c
#include <mxl/mxl.h>
#include <mxl/flow.h>
#include <mxl/time.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Calculate v210 grain size
// v210 packs 3 10-bit components into 4 bytes (32 bits)
// For 4:2:2 sampling: 2 pixels = 4 samples (Y0 Cb Y1 Cr) = 16 bytes
size_t calculate_v210_grain_size(uint32_t width, uint32_t height) {
    // Each line: width * 8/3 bytes (rounded up to 128-byte alignment)
    size_t line_bytes = ((width * 8 + 2) / 3);  // Round up division
    line_bytes = ((line_bytes + 127) / 128) * 128;  // Align to 128 bytes
    return line_bytes * height;
}

int write_v210_grain(mxlInstance instance, mxlFlowWriter writer) {
    // Get current TAI time as origin timestamp
    struct timespec ts;
    clock_gettime(CLOCK_TAI, &ts);
    uint64_t originTimestamp = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    // Get flow info to determine grain rate
    mxlFlowInfo flowInfo;
    mxlStatus status = mxlFlowWriterGetInfo(instance, writer, &flowInfo);
    if (status != MXL_STATUS_OK) {
        fprintf(stderr, "Failed to get flow info\n");
        return -1;
    }

    // Convert timestamp to grain index
    // grainIndex = timestamp / (grainRateDenominator * 1e9 / grainRateNumerator)
    uint64_t grainDurationNs = mxlRationalToNanoseconds(
        1,
        flowInfo.config.common.grainRate
    );
    uint64_t grainIndex = originTimestamp / grainDurationNs;

    // Open the grain for writing
    mxlGrainInfo grainInfo;
    uint8_t* buffer = NULL;

    status = mxlFlowWriterOpenGrain(instance, writer, grainIndex, &grainInfo, &buffer);
    if (status != MXL_STATUS_OK) {
        fprintf(stderr, "Failed to open grain at index %llu: %d\n", grainIndex, status);
        return -1;
    }

    printf("Opened grain %llu, size: %zu bytes, buffer: %p\n",
           grainIndex, grainInfo.grainSize, buffer);

    // Fill the grain with test pattern
    // v210 format: 32-bit words with Cb[9:0] Y0[19:10] Cr[29:20]
    uint32_t* pixels = (uint32_t*)buffer;
    size_t word_count = grainInfo.grainSize / 4;

    for (size_t i = 0; i < word_count; i++) {
        // Create a simple test pattern: grey ramp
        uint32_t luma = 64 + (i % 896);  // Video range 64-960
        uint32_t cb = 512;  // Neutral chroma
        uint32_t cr = 512;

        // Pack into v210 format (depends on position in sequence)
        switch (i % 4) {
            case 0: pixels[i] = (cr << 20) | (luma << 10) | cb; break;
            case 1: pixels[i] = (luma << 20) | (cb << 10) | luma; break;
            case 2: pixels[i] = (cb << 20) | (luma << 10) | cr; break;
            case 3: pixels[i] = (luma << 20) | (cr << 10) | luma; break;
        }
    }

    // Mark the entire grain as committed
    grainInfo.committedSize = grainInfo.grainSize;
    grainInfo.originTimestamp = originTimestamp;

    status = mxlFlowWriterCommitGrain(instance, writer, &grainInfo);
    if (status != MXL_STATUS_OK) {
        fprintf(stderr, "Failed to commit grain: %d\n", status);
        return -1;
    }

    printf("Successfully committed grain %llu with timestamp %llu\n",
           grainIndex, originTimestamp);

    return 0;
}
```

## Line-by-line explanation

1. **Timestamp acquisition**: Use `clock_gettime(CLOCK_TAI, ...)` to get TAI time. MXL uses TAI (not UTC) to avoid leap second discontinuities.

2. **Grain index calculation**: Convert the timestamp to a grain index by dividing by the grain duration. For 29.97fps (30000/1001), each grain represents ~33.37ms.

3. **Opening the grain**: `mxlFlowWriterOpenGrain` returns a pointer to the grain buffer. This buffer is memory-mapped and can be written directly.

4. **Filling the buffer**: Write your v210 pixel data. The example shows a simple grey ramp pattern. Real implementations would copy from a camera, decoder, or generator.

5. **Committing**: Set `committedSize` to `grainSize` and call `mxlFlowWriterCommitGrain`. The grain is now visible to readers.

## Progressive vs sliced writes

You can write a grain in multiple slices:

```c
// Open grain
mxlFlowWriterOpenGrain(instance, writer, grainIndex, &grainInfo, &buffer);

// Write first half of lines
size_t halfSize = grainInfo.grainSize / 2;
memcpy(buffer, mySourceBuffer, halfSize);
grainInfo.committedSize = halfSize;
mxlFlowWriterCommitGrain(instance, writer, &grainInfo);

// Readers can now access the first half while you prepare the second half

// Write second half
memcpy(buffer + halfSize, mySourceBuffer + halfSize, halfSize);
grainInfo.committedSize = grainInfo.grainSize;
mxlFlowWriterCommitGrain(instance, writer, &grainInfo);
```

This is useful for low-latency pipelines where readers start processing before the entire frame arrives.

---

Back to [Usage overview](./Usage.md)
