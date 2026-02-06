<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Basic Setup

This document covers the essential setup steps for creating and managing MXL instances, flow writers, and flow readers.

## 1. Create an MXL instance

```c
#include <mxl/mxl.h>

mxlInstance instance = mxlCreateInstance("/dev/shm/mxl", NULL);
if (!instance) {
    fprintf(stderr, "Failed to create MXL instance\n");
    return -1;
}
```

The instance handle represents your connection to an MXL domain. All subsequent operations require this handle. The domain path ("/dev/shm/mxl") should point to a tmpfs-backed directory with appropriate permissions.

## 2. Create a Flow Writer

```c
#include <mxl/flow.h>

const char* flowDef = "{...}"; // NMOS Flow Resource JSON describing the flow desired
mxlFlowWriter writer;
bool wasCreated;
mxlFlowWriterOptions writerOptions = {};
writerOptions.maxCommitBatchSizeHint = 1;
writerOptions.maxSyncBatchSizeHint = 1;

mxlStatus status = mxlCreateFlowWriter(
    instance,
    flowDef,
    &writerOptions,
    &writer,
    NULL,
    &wasCreated
);

if (status != MXL_STATUS_OK) {
    fprintf(stderr, "Failed to create flow writer: %d\n", status);
    mxlDestroyInstance(instance);
    return -1;
}

if (wasCreated) {
    printf("Created new flow\n");
} else {
    printf("Attached to existing flow\n");
}
```

The `wasCreated` flag tells you whether you created a new flow or attached to an existing one. Multiple writers can attach to the same flow, but typically only one writer actively writes grains to avoid conflicts.

## 3. Create a Flow Reader

```c
mxlFlowReader reader;
const char* flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

status = mxlCreateFlowReader(instance, flowId, NULL, &reader);
if (status != MXL_STATUS_OK) {
    fprintf(stderr, "Failed to create flow reader: %d\n", status);
    return -1;
}
```

Readers are lightweight and read-only. You can create multiple readers for the same flow from different processes.

## 4. Release Resources

Always release resources in reverse order of creation:

```c
mxlReleaseFlowReader(instance, reader);
mxlReleaseFlowWriter(instance, writer);
mxlDestroyInstance(instance);
```

---

Back to [Usage overview](./Usage.md)
