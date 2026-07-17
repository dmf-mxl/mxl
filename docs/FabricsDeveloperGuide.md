<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Fabrics Developer Guide

This guide covers the `mxl-fabrics` C API for transferring MXL flows between hosts over network fabrics (RDMA, TCP, EFA, shared memory). It assumes familiarity with the MXL Flow API (`mxl/flow.h`) and basic flow reader/writer usage as described in [Usage](./Usage.md). For the internal design and implementation details see [Fabrics](./Fabrics.md).

All definitions referenced here are in `mxl/fabrics.h`.

## Building

Build the CMake project with `MXL_ENABLE_FABRICS_OFI=ON`:

```bash
cmake --preset Linux-GCC-Debug -DMXL_ENABLE_FABRICS_OFI=ON .
```

This builds `libmxl-fabrics.so` and adds the CMake target `mxl::mxl-fabrics`.

## Linking

With CMake:

```cmake
find_package(mxl REQUIRED CONFIG)

target_link_libraries(my-app PUBLIC mxl::mxl mxl::mxl-fabrics)
```

With pkg-config:

```bash
pkg-config --libs --cflags libmxl-fabrics
```

## Creating an instance

Wrap an existing `mxlInstance` for use with `mxl-fabrics`:

```c
#include <mxl/mxl.h>
#include <mxl/fabrics.h>

mxlInstance instance = mxlCreateInstance("/dev/shm/mxl", NULL);

mxlFabricsInstance fabricsInstance;
mxlStatus status = mxlFabricsCreateInstance(instance, NULL, &fabricsInstance);
if (status != MXL_STATUS_OK) {
    // handle error
}
```

The `mxlFabricsInstance` holds resources shared across all targets and initiators. The `mxlInstance` it wraps must remain valid for the lifetime of the `mxlFabricsInstance`.

## Interfaces

Enumerate available network interfaces and their capabilities with `mxlFabricsGetInterfaces()`:

```c
mxlFabricsInterfaceList* list = NULL;
mxlFabricsGetInterfaces(fabricsInstance, NULL, &list);

for (mxlFabricsInterfaceList* entry = list; entry != NULL; entry = entry->next) {
    mxlFabricsInterfaceConfig const* iface = &entry->interface;
    // inspect iface->provider, iface->address, iface->caps
}

mxlFabricsFreeInterfaceList(list);
```

Pass an `mxlFabricsInterfaceConfig` as the query parameter to filter by provider or address. Setting `provider` to `MXL_FABRICS_PROVIDER_ANY` or passing `NULL` returns all interfaces.

Each returned entry represents a single combination of physical interface, address and provider. The same physical interface may appear multiple times if it is reachable through multiple providers or has multiple addresses assigned.

### Providers

| Enum value | Description |
| --- | --- |
| `MXL_FABRICS_PROVIDER_TCP` | Linux TCP sockets. Available everywhere, no special hardware required. |
| `MXL_FABRICS_PROVIDER_VERBS` | Userspace verbs (libibverbs) with librdmacm for connection management. Requires RDMA-capable hardware (e.g. Mellanox ConnectX). |
| `MXL_FABRICS_PROVIDER_EFA` | AWS Elastic Fabric Adapter. Available on supported EC2 instance types. |
| `MXL_FABRICS_PROVIDER_SHM` | Shared memory. For transfers between processes on the same host. |

### Capabilities

The `caps.flags` field is a bitmask of `mxlFabricsInterfaceCapFlags` values:

| Flag | Description |
| --- | --- |
| `MXL_FABRICS_IFACE_CAP_REMOTE_WRITE` | The interface supports remote write (RDMA) operations. This is the preferred transfer mode: the initiator writes directly into the target's memory without involvement from the target side. |
| `MXL_FABRICS_IFACE_CAP_SEND_RECEIVE` | The interface supports send/receive message operations. Used as a fallback when remote write is unavailable. Requires a bounce buffer and an extra memory copy on the target side. |
| `MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS` | The interface supports blocking wait operations (`mxlFabricsTargetReadGrain()`, `mxlFabricsInitiatorMakeProgressBlocking()`). |

When these flags are returned by `mxlFabricsGetInterfaces()`, they are informational: they describe what the interface supports. When passed to a setup function (through `mxlFabricsInterfaceConfig`), they express requirements. At least one of `REMOTE_WRITE` or `SEND_RECEIVE` must be set; if neither is set, `REMOTE_WRITE` is assumed.

### Maximum message size

The `caps.maxMessageSize` field reports the maximum length of a single data transfer for the interface. For most providers this is large enough to fit a full uncompressed video frame. If it is not, grains must be transferred slice by slice.

### Compatibility

Both sides (target and initiator) must receive the same capabilities and maximum message size to be compatible. There is no internal negotiation. Typically you would serialize the selected `mxlFabricsInterfaceConfig` alongside the `mxlFabricsTargetInfo` and share both with the initiator through your out-of-band signalling channel.

## Setting up a target

A target is the logical receiver of media data. It wraps an existing flow writer.

```c
// The flow writer must belong to the mxlInstance wrapped by fabricsInstance.
mxlFlowWriter writer;
// ... create writer with mxlCreateFlowWriter() ...

// Pick an interface from the enumerated list.
mxlFabricsInterfaceConfig ifaceConfig = list->interface;

mxlFabricsTarget target;
mxlFabricsCreateTarget(fabricsInstance, &target);

mxlFabricsTargetConfig config = {
    .version = MXL_FABRICS_API_VERSION,
    .interface = ifaceConfig,
    .writer = writer,
};

mxlFabricsTargetInfo targetInfo;
mxlStatus status = mxlFabricsTargetSetup(target, &config, NULL, &targetInfo);
if (status != MXL_STATUS_OK) {
    // handle error
}
```

The `mxlFabricsTargetSetup()` function returns a `mxlFabricsTargetInfo` object that contains the target's fabric address, remote memory keys and buffer addresses. This object must be shared with the initiator through an out-of-band mechanism (file, network message, etc.).

The `in_options` parameter accepts a JSON string. Currently the following option is recognized:

| Option | Type | Description |
| --- | --- | --- |
| `cqDepth` | number >= 1 | Depth of the target's completion queue. Increase this for high-frame-rate or many-stream receivers. See [Fabrics - Receiving grains](./Fabrics.md) for sizing guidance. |

### Serializing the TargetInfo

Serialize to a string for transmission:

```c
size_t len = 0;
mxlFabricsTargetInfoToString(targetInfo, NULL, &len);

char* str = malloc(len);
mxlFabricsTargetInfoToString(targetInfo, str, &len);
// send 'str' to the initiator
free(str);
```

On the initiator side, deserialize:

```c
mxlFabricsTargetInfo remoteTargetInfo;
mxlFabricsTargetInfoFromString(receivedString, &remoteTargetInfo);
```

The serialization format is JSON. The format may change between library versions, but forward compatibility is maintained: an initiator built from an earlier version of `libmxl-fabrics` will understand a target info produced by a later version.

The target info contains memory region addresses and an access key (the RDMA remote key). This key exists to prevent accidental writes to the wrong memory region; it is not a security mechanism. Any process that can reach the target's network endpoint and knows the key can write to the registered memory. Do not rely on the target info for access protection. Media transfers are unencrypted. Restrict access to the fabric network itself using network ACLs, firewalls or similar infrastructure-level controls.

## Setting up an initiator

An initiator is the logical sender of media data. It wraps an existing flow reader.

```c
mxlFlowReader reader;
// ... create reader with mxlCreateFlowReader() ...

mxlFabricsInitiator initiator;
mxlFabricsCreateInitiator(fabricsInstance, &initiator);

mxlFabricsInitiatorConfig config = {
    .version = MXL_FABRICS_API_VERSION,
    .interface = ifaceConfig,
    .reader = reader,
};

mxlFabricsInitiatorSetup(initiator, &config, NULL);
```

Select an interface from `mxlFabricsGetInterfaces()` that is compatible with the target's interface (same provider, matching capabilities).

After setup, connect to one or more targets by adding them:

```c
mxlFabricsInitiatorAddTarget(initiator, remoteTargetInfo);
```

This function is non-blocking. If the underlying provider requires additional connection setup, it happens during subsequent calls to `mxlFabricsInitiatorMakeProgress*()`. Wait for connection establishment to complete before starting transfers:

```c
mxlStatus s;
do {
    s = mxlFabricsInitiatorMakeProgressBlocking(initiator, 250);
} while (s == MXL_ERR_NOT_READY);

if (s != MXL_STATUS_OK) {
    // connection failed
}
```

An initiator can be connected to multiple targets at the same time. Each call to `mxlFabricsInitiatorTransferGrain()` or `mxlFabricsInitiatorTransferSamples()` enqueues the transfer to all currently added targets. This allows one-to-many distribution from a single initiator without additional application logic.

To disconnect from a target:

```c
mxlFabricsInitiatorRemoveTarget(initiator, remoteTargetInfo);
```

This is also non-blocking. The disconnection completes when `mxlFabricsInitiatorMakeProgress*()` stops returning `MXL_ERR_NOT_READY`. No new transfers will be queued to the removed target, but in-flight transfers may still complete during progress calls.

## Progress

Progress on media transfers, connection establishment and teardown is driven by polling:

- On the **initiator** side, call `mxlFabricsInitiatorMakeProgressNonBlocking()` or `mxlFabricsInitiatorMakeProgressBlocking()`.
- On the **target** side, call `mxlFabricsTargetReadGrainNonBlocking()` / `mxlFabricsTargetReadGrain()` or `mxlFabricsTargetReadSamplesNonBlocking()` / `mxlFabricsTargetReadSamples()`.

These functions return `MXL_ERR_NOT_READY` when there is pending work that has not completed yet. They must be called regularly, because connection state transitions (connecting, connected, disconnecting) also happen during these calls.

## Moving media

The fabrics API does not access readers and writers directly. It replicates data between the underlying buffers. The application is still responsible for reading grains/samples on the initiator side and committing them on the target side using the regular flow API. On the target side, the remote initiator writes both the media payload and the grain/sample metadata directly into the local ring buffer, so `mxlFlowWriterOpenGrain` and `mxlFlowWriterOpenSamples` return the already-populated data and the application only needs to commit.

### Transferring discrete flows (grains)

#### Initiator side

Read a grain using the flow API, then enqueue it for transfer:

```c
mxlGrainInfo grainInfo;
uint8_t* payload;
mxlStatus s = mxlFlowReaderGetGrain(reader, grainIndex, timeout, &grainInfo, &payload);

if (s == MXL_ERR_OUT_OF_RANGE_TOO_LATE) {
    // The reader fell behind and the grain was overwritten in the ring buffer.
    // Skip ahead to the current index.
    grainIndex = mxlGetCurrentIndex(&grainRate);
    // retry with the new index
}

if (s == MXL_ERR_OUT_OF_RANGE_TOO_EARLY) {
    // The grain has not been written yet. Retry the same index later.
}

if (s == MXL_ERR_TIMEOUT) {
    // The grain did not become available before the timeout expired.
    // This usually indicates a problem upstream (the writer stopped producing).
}
```

When the grain is available, check whether it is marked as invalid. Invalid grains signal that the upstream writer had no valid data (e.g. it timed out waiting for input). Transfer only the grain header to avoid wasting bandwidth:

```c
if (grainInfo.flags & MXL_GRAIN_FLAG_INVALID) {
    mxlFabricsInitiatorTransferGrain(initiator, grainIndex, 0, 0);
} else {
    // Transfer the full grain.
    mxlFabricsInitiatorTransferGrain(initiator, grainIndex, 0, grainInfo.totalSlices);

    // Or transfer a slice range (startSlice inclusive, endSlice exclusive).
    mxlFabricsInitiatorTransferGrain(initiator, grainIndex, startSlice, endSlice);
}
```

`mxlFabricsInitiatorTransferGrain()` is non-blocking. The transfer may start immediately, but is only guaranteed to have completed after `mxlFabricsInitiatorMakeProgress*()` stops returning `MXL_ERR_NOT_READY`:

```c
while (mxlFabricsInitiatorMakeProgressNonBlocking(initiator) == MXL_ERR_NOT_READY) {
    // optionally do other work
}
```

Or with the blocking variant:

```c
while (mxlFabricsInitiatorMakeProgressBlocking(initiator, 100) == MXL_ERR_NOT_READY) {
    // waited up to 100ms, still in progress
}
```

#### Target side

Poll for completed grain transfers:

```c
uint64_t grainIndex;
mxlStatus s = mxlFabricsTargetReadGrainNonBlocking(target, &grainIndex);
if (s == MXL_STATUS_OK) {
    // The grain data and grain info were already written into the local ring buffer
    // by the remote initiator. Open and commit to make it visible to local readers.
    mxlGrainInfo info;
    uint8_t* buffer;
    mxlFlowWriterOpenGrain(writer, grainIndex, &info, &buffer);
    mxlFlowWriterCommitGrain(writer, &info);
}
```

Or with the blocking variant:

```c
uint64_t grainIndex;
mxlStatus s = mxlFabricsTargetReadGrain(target, 200 /*ms*/, &grainIndex);
```

### Transferring continuous flows (samples)

#### Initiator side

Read samples using the flow API, then enqueue them for transfer:

```c
mxlWrappedMultiBufferSlice slices;
mxlStatus s = mxlFlowReaderGetSamplesNonBlocking(reader, headIndex, count, &slices);

if (s == MXL_ERR_OUT_OF_RANGE_TOO_LATE) {
    // The reader fell behind. Skip ahead to the current head index.
    mxlFlowRuntimeInfo runtime;
    mxlFlowReaderGetRuntimeInfo(reader, &runtime);
    headIndex = runtime.headIndex;
    // retry with the new index
}

if (s == MXL_ERR_OUT_OF_RANGE_TOO_EARLY) {
    // The samples have not been written yet. Retry the same index later.
}
```

When the samples are available, enqueue the transfer and wait for completion:

```c
mxlFabricsInitiatorTransferSamples(initiator, headIndex, count);

while (mxlFabricsInitiatorMakeProgressNonBlocking(initiator) == MXL_ERR_NOT_READY) {
    // wait for completion
}

headIndex += count;
```

#### Target side

Poll for completed sample transfers:

```c
uint64_t headIndex;
size_t count;
mxlStatus s = mxlFabricsTargetReadSamplesNonBlocking(target, &headIndex, &count);
if (s == MXL_STATUS_OK) {
    // Samples are in the local ring buffer. Commit them through the flow API.
    mxlMutableWrappedMultiBufferSlice scratch;
    mxlFlowWriterOpenSamples(writer, headIndex, count, &scratch);
    // scratch already contains the transferred data
    mxlFlowWriterCommitSamples(writer);
}
```

Or with the blocking variant:

```c
uint64_t headIndex;
size_t count;
mxlStatus s = mxlFabricsTargetReadSamples(target, 200 /*ms*/, &headIndex, &count);
```

## Demo application

The `tools/mxl-fabrics-demo/` directory contains a working command-line application that implements both a target and an initiator for discrete and continuous flows. It covers interface selection, target info exchange, error handling and graceful shutdown. Run it with `--help` for usage. A typical session:

```bash
# Start a target (receiver):
./mxl-fabrics-demo -d /dev/shm/mxl -f flow.json --service 1234

# The target prints a base64-encoded target info string to stdout.
# Pass it to the initiator:
./mxl-fabrics-demo -i -d /dev/shm/mxl -f <flow-uuid> --service 1234 --target-info <base64-string>
```

The demo selects the best available provider automatically (EFA > Verbs > TCP > SHM). Use `--node` to bind to a specific address and `--provider` to force a specific provider.

## Teardown

Destroy objects in reverse order of creation:

```c
// 1. Disconnect targets from the initiator
mxlFabricsInitiatorRemoveTarget(initiator, remoteTargetInfo);
while (mxlFabricsInitiatorMakeProgressNonBlocking(initiator) == MXL_ERR_NOT_READY) {}

// 2. Destroy initiators and targets
mxlFabricsDestroyInitiator(fabricsInstance, initiator);
mxlFabricsDestroyTarget(fabricsInstance, target);

// 3. Free target info objects
mxlFabricsFreeTargetInfo(targetInfo);
mxlFabricsFreeTargetInfo(remoteTargetInfo);

// 4. Destroy the fabrics instance
mxlFabricsDestroyInstance(fabricsInstance);

// 5. Release flow readers/writers and destroy the mxl instance
mxlReleaseFlowWriter(instance, writer);
mxlReleaseFlowReader(instance, reader);
mxlDestroyInstance(instance);
```
