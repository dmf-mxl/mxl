# <!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
# <!-- SPDX-License-Identifier: CC-BY-4.0 -->
#
# Using the MXL C Public API

Below is a minimal example of how to use the MXL SDK C API in your application:

1. **Create an MXL instance**

```c
#include <mxl/mxl.h>

mxlInstance instance = mxlCreateInstance("/dev/shm/mxl", NULL);
if (!instance) {
    // handle error
}
```

2. **Create a Flow Writer**

```c
#include <mxl/flow.h>

const char* flowDef = "{...}"; // NMOS Flow Resource JSON describing the flow desired
mxlFlowWriter writer;
bool wasCreated;
mxlStatus status = mxlCreateFlowWriter(instance, flowDef, NULL, &writer, NULL, &wasCreated);
if (status != MXL_STATUS_OK) {
    // handle error
}
```

3. **Create a Flow Reader**

```c
mxlFlowReader reader;
status = mxlCreateFlowReader(instance, "<flow-id>", NULL, &reader);
if (status != MXL_STATUS_OK) {
    // handle error
}
```

4. **Read/Write Data**

- For discrete flows, use `mxlFlowReaderGetGrain` and `mxlFlowWriterOpenGrain`/`mxlFlowWriterCommitGrain`.
- For continuous flows, use `mxlFlowReaderGetSamples` and `mxlFlowWriterOpenSamples`/`mxlFlowWriterCommitSamples`.

5. **Release Resources**

```c
mxlReleaseFlowWriter(instance, writer);
mxlReleaseFlowReader(instance, reader);
mxlDestroyInstance(instance);
```

See the header files in `lib/include/mxl/` for full API details and additional options.
