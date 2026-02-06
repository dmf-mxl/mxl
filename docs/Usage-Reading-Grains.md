<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Reading Grains

Reading is simpler than writing because you don't need to calculate indices.

## Reading the latest grain

```c
#include <mxl/mxl.h>
#include <mxl/flow.h>

int read_latest_grain(mxlInstance instance, mxlFlowReader reader) {
    // Get current runtime info to find the head index
    mxlFlowRuntimeInfo runtimeInfo;
    mxlStatus status = mxlFlowReaderGetRuntimeInfo(instance, reader, &runtimeInfo);
    if (status != MXL_STATUS_OK) {
        fprintf(stderr, "Failed to get runtime info\n");
        return -1;
    }

    uint64_t headIndex = runtimeInfo.headIndex;
    printf("Current head index: %llu\n", headIndex);

    // Read the grain at the head
    mxlGrainInfo grainInfo;
    const uint8_t* buffer = NULL;
    uint64_t timeout_ns = 100000000;  // 100ms timeout

    status = mxlFlowReaderGetGrain(instance, reader, headIndex, timeout_ns, &grainInfo, &buffer);
    if (status == MXL_STATUS_TIMEOUT) {
        printf("Timeout waiting for grain\n");
        return -1;
    } else if (status != MXL_STATUS_OK) {
        fprintf(stderr, "Failed to read grain: %d\n", status);
        return -1;
    }

    printf("Read grain %llu:\n", headIndex);
    printf("  Size: %zu bytes\n", grainInfo.grainSize);
    printf("  Committed: %zu bytes\n", grainInfo.committedSize);
    printf("  Origin timestamp: %llu ns\n", grainInfo.originTimestamp);
    printf("  Buffer: %p\n", buffer);

    // Process the grain
    if (grainInfo.committedSize == grainInfo.grainSize) {
        // Grain is complete
        process_complete_grain(buffer, grainInfo.grainSize);
    } else {
        // Grain is partial (writer is still filling it)
        printf("Grain is partial, only %zu bytes available\n", grainInfo.committedSize);
    }

    return 0;
}
```

## Read grains in a continuous loop

```c
int read_grain_loop(mxlInstance instance, mxlFlowReader reader) {
    uint64_t last_read_index = 0;
    bool first_read = true;

    while (true) {
        mxlFlowRuntimeInfo runtimeInfo;
        mxlStatus status = mxlFlowReaderGetRuntimeInfo(instance, reader, &runtimeInfo);
        if (status != MXL_STATUS_OK) continue;

        uint64_t headIndex = runtimeInfo.headIndex;

        // On first read, start from head
        if (first_read) {
            last_read_index = headIndex;
            first_read = false;
        }

        // Check if there are new grains
        if (headIndex > last_read_index) {
            // Read all grains between last_read_index and headIndex
            for (uint64_t idx = last_read_index + 1; idx <= headIndex; idx++) {
                mxlGrainInfo grainInfo;
                const uint8_t* buffer = NULL;

                status = mxlFlowReaderGetGrain(instance, reader, idx, 0, &grainInfo, &buffer);
                if (status == MXL_STATUS_OK) {
                    printf("Processing grain %llu\n", idx);
                    process_grain(buffer, grainInfo.committedSize);
                    last_read_index = idx;
                }
            }
        }

        // Sleep briefly to avoid busy-waiting
        usleep(1000);  // 1ms
    }

    return 0;
}
```

## Key points

- Use `mxlFlowReaderGetRuntimeInfo` to find the current head index.
- `mxlFlowReaderGetGrain` blocks until the grain is available (or timeout expires).
- Always check `committedSize` vs `grainSize` to handle partial grains.
- The buffer pointer is read-only and memory-mapped. Do not write to it.

---

Back to [Usage overview](./Usage.md)
