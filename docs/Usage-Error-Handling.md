<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Error Handling Best Practices

Always check return status codes and handle errors appropriately.

## Helper macro for error checking

```c
#include <mxl/mxl.h>

// Helper macro for error checking
#define MXL_CHECK(call, msg) do { \
    mxlStatus status = (call); \
    if (status != MXL_STATUS_OK) { \
        fprintf(stderr, "%s failed with status %d\n", msg, status); \
        goto error; \
    } \
} while(0)

int robust_mxl_operation() {
    mxlInstance instance = NULL;
    mxlFlowWriter writer = NULL;
    mxlFlowReader reader = NULL;
    int result = -1;

    // Create instance
    instance = mxlCreateInstance("/dev/shm/mxl", NULL);
    if (!instance) {
        fprintf(stderr, "Failed to create MXL instance\n");
        goto error;
    }

    // Create writer
    const char* flow_def = "{...}";
    bool was_created;
    MXL_CHECK(mxlCreateFlowWriter(instance, flow_def, NULL, &writer, NULL, &was_created),
              "mxlCreateFlowWriter");

    // Create reader
    const char* flow_id = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    MXL_CHECK(mxlCreateFlowReader(instance, flow_id, NULL, &reader),
              "mxlCreateFlowReader");

    // Perform operations
    // ...

    result = 0;  // Success

error:
    // Cleanup in reverse order
    if (reader) mxlReleaseFlowReader(instance, reader);
    if (writer) mxlReleaseFlowWriter(instance, writer);
    if (instance) mxlDestroyInstance(instance);

    return result;
}
```

## Common error codes

- `MXL_STATUS_OK` (0): Success
- `MXL_STATUS_TIMEOUT`: Operation timed out waiting for data
- `MXL_STATUS_INVALID_ARGUMENT`: Invalid parameter passed
- `MXL_STATUS_NOT_FOUND`: Flow or resource not found
- `MXL_STATUS_PERMISSION_DENIED`: Insufficient permissions
- `MXL_STATUS_OUT_OF_MEMORY`: Memory allocation failed

---

Back to [Usage overview](./Usage.md)
