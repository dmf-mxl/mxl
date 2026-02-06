<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Configuration: Advanced configuration

## Custom history duration per flow

While the domain-level `history_duration` option sets a default, you can override it for individual flows by specifying the grain count in the writer options:

```c
// Calculate custom grain count
uint64_t custom_history_ns = 500000000;  // 500ms
uint64_t grain_duration_ns = mxlRationalToNanoseconds(1, grain_rate);
uint32_t custom_grain_count = (custom_history_ns + grain_duration_ns - 1) / grain_duration_ns;

// Pass to writer options
mxlFlowWriterOptions options = {};
options.customGrainCount = custom_grain_count;  // If supported by API

// Create writer
mxlCreateFlowWriter(instance, flow_def, &options, &writer, NULL, &was_created);
```

Check `lib/include/mxl/flow.h` for the current API to see if per-flow grain count is supported.

## Batch size tuning

For low-latency video pipelines, reduce batch sizes to wake readers more frequently:

```c
// Low-latency configuration (wake readers every scanline)
mxlFlowWriterOptions options = {};
options.maxCommitBatchSizeHint = 1;     // Commit one scanline at a time
options.maxSyncBatchSizeHint = 1;       // Sync after every commit
```

For high-throughput audio pipelines, increase batch sizes:

```c
// High-throughput configuration (larger audio batches)
mxlFlowWriterOptions options = {};
options.maxCommitBatchSizeHint = 4096;  // 4096 samples per batch (85ms at 48kHz)
options.maxSyncBatchSizeHint = 4096;
```

Trade-offs:
- Smaller batches = lower latency, more futex wake-ups, higher CPU overhead
- Larger batches = higher latency, fewer wake-ups, lower CPU overhead

[Back to Configuration overview](./Configuration.md)
