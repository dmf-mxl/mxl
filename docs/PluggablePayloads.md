<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Pluggable payloads

A discrete flow keeps its ring, grain headers, and synchronization in host shared memory. The payload of each grain is held by a `PayloadStorage` object. Host storage is built into libmxl: the payload follows the 8 KiB grain header in the grain file. Any other storage is a backend library that ships with the SDK. libmxl loads that library by name when a flow uses it.

This page summarizes the backend interface and how an application uses it. [Grain Payload Storage](PayloadStorage.md) is the longer design, including storage types that are not implemented yet.

## What an application calls

A writer that wants the built-in host storage passes no `payload` object, or sets `backend` to `host`. A writer that wants another storage names the backend and passes that backend's own keys in the same object. libmxl reads only `backend`. It passes every other key to the backend as a JSON object.

A CUDA writer names the GPU by UUID, the value `nvidia-smi -L` prints:

```json
{"payload": {"backend": "cuda-linear", "deviceUuid": "GPU-88064374-43da-caf5-b332-be5fb39bf2e6"}}
```

A reader cannot set `backend`. The flow already chose the storage. A reader may pass backend keys, for example a `deviceUuid`, so a CUDA payload can be mapped on another GPU of the same machine. Host flows ignore those keys.

After the reader or writer exists, an application that handles any storage type does this:

1. Read the layout once with `mxlFlowReaderGetStorageLayout` or `mxlFlowWriterGetStorageLayout`. The layout holds the storage type, the slot count, and the offset, size, and pitch of each plane. Storage types that name a device describe it in the reserved bytes of the layout.
2. Call `mxlFlowReaderMapSlots` or `mxlFlowWriterMapSlots` once, with an array of `slotCount` entries. That call imports the payload into this process and fills one `mxlGrainStorage` per slot.
3. In the grain loop, call `mxlFlowReaderGetGrainSlot` or `mxlFlowWriterOpenGrainSlot` and use the returned slot as an index into that array. Commit and cancel are unchanged.

`mxlFlowReaderGetGrain` and `mxlFlowWriterOpenGrain` still return a host pointer for host storage. For any other storage type they return `MXL_ERR_UNSUPPORTED_OPERATION`.

`mxlCommonFlowConfigInfo.payloadLocation` and `deviceIndex` are deprecated. libmxl leaves them at 0 and -1.

## Storage types

`mxlGrainStorageLayout.storageType` is a null terminated name. The names libmxl defines have no dot. `MXL_PAYLOAD_STORAGE_HOST_POINTER` is `"host-pointer"`. A backend that defines its own storage type uses a reversed domain name, so libmxl copies the reserved bytes of the layout and of each slot without reading them.

The `cuda-linear` backend uses `com.nvidia.cuda-device-pointer`. Its definitions are in `lib/payload-backends/cuda-linear/CudaLinearStorage.h`:

- `mxlCudaLinearLayoutInfo` is written at the start of `mxlGrainStorageLayout.reserved`. It holds the device UUID that holds the payload and the device index in the reading process, or -1.
- `mxlCudaLinearSlotStorage` is written at the start of `mxlGrainStorage.reserved`. It holds the CUDA device pointer of the slot, valid in the calling process.

## Files in a backend flow

A flow whose payload is held by a backend uses flow data version 2. Its grain files hold only the grain header. Its directory holds `payload.json`, written by libmxl before the flow is published:

```json
{
  "version": 1.0,
  "backend": "cuda-linear",
  "storageType": "com.nvidia.cuda-device-pointer"
}
```

A reader uses `backend` to choose the library. A reader that finds another `version` does not open the flow.

The backend writes any extra files it needs in the same directory. `cuda-linear` writes `payload.cuda.json`: the descriptor version, the export format `posix-fd`, the device UUID, the slot count, the stride, and the allocation size. The file descriptor itself is not in that file. libmxl does not read `payload.cuda.json`.

## When the library is loaded

The library name is `libmxl-payload-<name>.so`. A name is at most 64 characters, starts with a lowercase letter or digit, and then contains only lowercase letters, digits, and hyphens. `host` is reserved for the built-in storage and is not loaded from a file.

libmxl searches the absolute directories in `MXL_PAYLOAD_PLUGIN_PATH`, separated by colons, and then the directory that holds libmxl. It uses the first directory that contains the file. Relative entries are ignored. The variable is ignored in a setuid or setgid process. A successful load stays mapped until the process exits. A later flow with the same name reuses that function table.

A writer loads the library before any flow directory is created. A missing library returns `MXL_ERR_UNSUPPORTED_OPERATION` and publishes nothing. A reader loads the library named in `payload.json` while opening the flow. If that load fails, the reader is still created and can read flow metadata. `mxlFlowReaderGetStorageLayout` and `mxlFlowReaderMapSlots` then return `MXL_ERR_UNSUPPORTED_OPERATION`. Opening a reader does not import device memory. `MapSlots` does that.

A backend flow has one writer. A second writer returns `MXL_ERR_CONFLICT`. Invalid `payload` options return `MXL_ERR_INVALID_ARG`. Continuous flows keep their payload in host memory.

## The backend interface

The interface is the internal header `lib/internal/include/mxl-internal/PayloadBackendAbi.h`. It is not installed, and an application cannot register a backend. Only C types cross the boundary. A backend function must not throw a C++ exception.

Each library exports one symbol, `mxlGetPayloadBackendApi`. libmxl calls it with interface version 1 (`MXL_PAYLOAD_BACKEND_ABI_VERSION`) and receives `mxlPayloadBackendApiV1`. The table's `name` must equal the name used to load the library. `storageType` must be a valid storage type name. The functions must all be set.

| Entry | Role |
|---|---|
| `prepare` | Writer. Allocate the payload, write import files in the staging flow directory, and start serving what readers need. Before the flow is published. |
| `open` | Reader, or a flow that already exists. Must not import device memory. |
| `destroy` | Release the instance, its allocations, and its imports. |
| `describeLayout` | Complete the layout. Set the device in the reserved bytes. May change plane pitch. Must not import memory. |
| `mapSlots` | Import every slot. Later calls return success and do no further work. |
| `slotStorage` | Fill `mxlGrainStorage` for one slot. Imports first if `mapSlots` was not called. |

libmxl passes a `mxlPayloadBackendFlowInfo`: slot count, grain payload size, access mode, plane count, slices per grain, slice sizes, and the flow's `mxlDataFormat`. libmxl does not choose which formats a backend may store. The backend rejects a format it cannot hold.

`PluginPayloadStorage` in libmxl is the `PayloadStorage` that forwards these calls. `FlowManager` stores it on the discrete flow with `setPayloadStorage` after `prepare` or `open` returns.

## The cuda-linear backend

`libmxl-payload-cuda-linear.so` is built when `MXL_ENABLE_CUDA_PAYLOAD` is on. It links the static CUDA runtime. libmxl does not link CUDA. The backend stores video flows only. It rejects other formats.

The writer resolves `deviceUuid` to a CUDA device in its own process, creates one virtual-memory allocation for every slot, aligns each slot to 256 bytes, and exports the allocation once as a POSIX file descriptor. The writer serves that descriptor on `payload.sock` in the flow directory. A reader in another process connects to `payload.sock` in `mapSlots`, receives the descriptor, and imports it. A reader in the writer's process shares the allocation through a registry inside the process. The allocation stays alive for that reader after the writer is released.

The socket sends the descriptor only to a process that may read the flow directory. A writer whose publish fails removes only the socket it bound.

A reader that passes a different visible `deviceUuid` maps the allocation with peer access when `cudaDeviceCanAccessPeer` allows it. Device writes for a grain must be finished before `mxlFlowWriterCommitGrain`.

Same-node writer and reader pairs share one MXL domain. The domain has to be visible to both, including the socket in the flow directory. They do not need a shared IPC namespace or a shared PID namespace. The reader must see the writer's GPU. A copy between nodes is a separate domain on each node. RDMA from device memory is later work, described in [Grain Payload Storage](PayloadStorage.md).
