<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Configuration: Flow-level configuration

Flow-level options are specified when creating a flow writer. These options apply only to the specific flow and can differ between flows in the same domain.

## Flow writer options

When calling `mxlCreateFlowWriter`, you can pass an `mxlFlowWriterOptions` structure:

```c
typedef struct mxlFlowWriterOptions {
    uint32_t maxCommitBatchSizeHint;  // Maximum grains/samples committed in one operation
    uint32_t maxSyncBatchSizeHint;    // Maximum grains/samples per synchronization event
    // Additional fields may be present; consult mxl/flow.h
} mxlFlowWriterOptions;
```

**maxCommitBatchSizeHint:**

For discrete flows: The number of scan lines or slices committed at once. This hint helps readers optimize futex wake-up frequency.

For continuous flows: The number of samples committed in each `mxlFlowWriterCommitSamples` call. Readers can use this to size their read windows.

**maxSyncBatchSizeHint:**

Indicates how many grains/samples are typically written before triggering a synchronization event. Readers waiting on futexes will wake up after this many units are committed.

**Example:**

```c
mxlFlowWriterOptions writer_options = {};
writer_options.maxCommitBatchSizeHint = 1080;  // One full scanline (1080p)
writer_options.maxSyncBatchSizeHint = 1080;

mxlFlowWriter writer;
bool was_created;
mxlStatus status = mxlCreateFlowWriter(
    instance,
    flow_def_json,
    &writer_options,  // Pass options here
    &writer,
    NULL,
    &was_created
);
```

## Flow reader options

Flow readers also support configuration options:

```c
typedef struct mxlFlowReaderOptions {
    // Options for reader behavior
    // Consult mxl/flow.h for current fields
} mxlFlowReaderOptions;
```

Readers typically have fewer options than writers, as their behavior is mostly reactive.

[Back to Configuration overview](./Configuration.md)
