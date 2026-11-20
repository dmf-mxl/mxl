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

1. Read the layout once with `mxlFlowReaderGetStorageLayout` or `mxlFlowWriterGetStorageLayout`. The layout holds the storage type, the slot count, and the offset, size, and pitch of each plane. Its reserved bytes hold what the storage type defines, such as the devices of the payload: for `cuda-linear`, the index in this process of the device on which it accesses the payload, and the UUID of the device that holds it, which `mxlGetCudaGrainStorageLayout()` copies out. The storage type's header defines those bytes, and is installed with its backend, not with libmxl: [The cuda-linear backend](#the-cuda-linear-backend).
2. Call `mxlFlowReaderMapSlots` or `mxlFlowWriterMapSlots` once, with an array of `slotCount` entries. That call imports the payload into this process and fills one `mxlGrainStorage` per slot. For a reader of storage shared as file descriptors, it is also when the descriptors arrive from the writer, as [Sharing descriptors](#sharing-descriptors) describes.
3. In the grain loop, call `mxlFlowReaderGetGrainSlot` or `mxlFlowWriterOpenGrainSlot` and use the returned slot as an index into that array. Commit and cancel are unchanged.

`mxlFlowReaderGetGrain` and `mxlFlowWriterOpenGrain` still return a host pointer for host storage. For any other storage type they return `MXL_ERR_UNSUPPORTED_OPERATION`.

`mxlGrainStorageLayout.storageType` tells a reader how the slots are addressed. `mxlCommonFlowConfigInfo.payloadLocation` and `deviceIndex` are deprecated, and are left at 0 and -1.

## Files in a backend flow

A flow whose payload is held by a backend uses flow data version 2. Its grain files hold only the grain header. Its directory holds `payload.json`, written by libmxl before the flow is published:

```json
{
  "version": 1.0,
  "backend": "cuda-linear",
  "storageType": "cuda-device-pointer"
}
```

`storageType` is the name of the storage type the backend reported. For `cuda-linear` it is `MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER`, `"cuda-device-pointer"`. A reader uses `backend` to choose the library. A reader that finds another `version` does not open the flow.

The backend writes any extra files it needs in the same directory. `cuda-linear` writes `payload.cuda.json`: the descriptor version, the export format `posix-fd`, the device UUID, the slot count, the stride, and the allocation size. The file descriptor itself is not in that file. libmxl passes it on `payload.sock`, which is in the same directory for every backend that shares descriptors ([Sharing descriptors](#sharing-descriptors)). libmxl does not read `payload.cuda.json`.

## Sharing descriptors

Some storage can only be shared as file descriptors, such as the exported memory of `cuda-linear`, dma-bufs or exported Vulkan memory. The writer's libmxl serves them on `payload.sock`, a Unix socket in the flow directory, and a reader's libmxl receives duplicates of them. [The descriptor channel](PayloadStorage.md#the-descriptor-channel) describes it in full. What an application, and whoever deploys it, needs to know:

- The writer serves the socket from the creation of the flow until the writer is released or its process exits.
- A reader receives the descriptors when it maps its slots, with `mxlFlowReaderMapSlots`. Opening a reader and reading its layout do not connect to the socket, so the flow metadata can be read without the writer.
- Mapping the slots of a flow whose writer is gone returns `MXL_ERR_INVALID_STATE`. Host flows have no such limit. A reader that mapped its slots before keeps its slot descriptions until it is released, and receives no new grains.
- Whoever may read the flow's `data` file may connect to the socket, and nobody else. The socket's write permission, which connecting needs, follows that file's read permission, and the kernel checks it with the reader's user and groups.
- Writer and reader run on the same machine, and see the same domain, on a file system that holds Unix sockets, such as a tmpfs. Containers share a flow by mounting the same domain directory. They need no shared IPC or PID namespace.
- The descriptors in the slot descriptions belong to libmxl, and stay valid until the reader is released. Duplicate one before passing it to a function that takes ownership of it, such as a CUDA, Vulkan or OpenGL import, or before keeping it longer.

A backend shares descriptors through two entries of its interface, `exportDescriptors` and `importDescriptors`, below. libmxl does the rest.

## When the library is loaded

The library name is `libmxl-payload-<name>.so`. A name is at most 64 characters, starts with a lowercase letter or digit, and then contains only lowercase letters, digits, and hyphens. `host` is reserved for the built-in storage and is not loaded from a file.

libmxl searches the absolute directories in `MXL_PAYLOAD_PLUGIN_PATH`, separated by colons, and then the directory that holds libmxl. It uses the first directory that contains the file. Relative entries are ignored. The variable is ignored in a setuid or setgid process. A successful load stays mapped until the process exits. A later flow with the same name reuses that function table.

A writer loads the library before any flow directory is created. A missing library returns `MXL_ERR_UNSUPPORTED_OPERATION` and publishes nothing. A reader loads the library named in `payload.json` while opening the flow. If that load fails, the reader is still created and can read flow metadata. `mxlFlowReaderGetStorageLayout` and `mxlFlowReaderMapSlots` then return `MXL_ERR_UNSUPPORTED_OPERATION`. Opening a reader does not import device memory. `MapSlots` does that.

A backend flow has one writer. A second writer returns `MXL_ERR_CONFLICT`. Invalid `payload` options return `MXL_ERR_INVALID_ARG`. Continuous and event flows keep their payload in host memory.

## The backend interface

The interface is the internal header `lib/internal/include/mxl-internal/PayloadBackendAbi.h`. It is not installed, and an application cannot register a backend. Only C types cross the boundary. A backend function must not throw a C++ exception.

Each library exports one symbol, `mxlGetPayloadBackendApi`. libmxl calls it with interface version 2 (`MXL_PAYLOAD_BACKEND_ABI_VERSION`) and receives `mxlPayloadBackendApi`. The table's `name` must equal the name used to load the library. `storageType` must be a valid storage type name, as described in [Storage types are names](PayloadStorage.md#storage-types-are-names). libmxl does not keep a list of storage types. The functions through `mapSlots` must be set. `capabilities` lists what else the backend does. A backend that shares its storage through file descriptors reports `MXL_PAYLOAD_BACKEND_CAP_SHARE_DESCRIPTORS` and sets `exportDescriptors` and `importDescriptors`. Any other backend leaves them null. A backend that defines media types of its own reports `MXL_PAYLOAD_BACKEND_CAP_DESCRIBE_GEOMETRY` and sets `describeGeometry`, which libmxl asks for the geometry of their grains, as [Media types a backend defines](PayloadStorage.md#media-types-a-backend-defines) describes. `cuda-linear` stores `video/v210`, which libmxl defines, and leaves it null. libmxl refuses a backend built for another interface version.

| Entry | Role |
|---|---|
| `prepare` | Writer. Allocate the payload and write import files in the temporary flow directory, before the flow is published. |
| `open` | Reader, or a flow that already exists. Must not import device memory. |
| `destroy` | Release the instance, its allocations, and its imports. |
| `describeLayout` | Complete the layout: may change the offset, size and pitch of planes, and fills the reserved bytes its storage type defines, such as the device. Must not change the storage type or import memory. |
| `mapSlots` | Import every slot. Later calls return success and do no further work. |
| `slotStorage` | Fill `mxlGrainStorage` for one slot. Imports first if `mapSlots` was not called. |
| `exportDescriptors` | Writer, after `prepare`. Report the file descriptors that share this storage. They stay the backend's, open until `destroy`. libmxl serves duplicates of them on `payload.sock`, as described in [The descriptor channel](PayloadStorage.md#the-descriptor-channel). Null when the backend shares none. |
| `importDescriptors` | Reader, before the first `mapSlots` or `slotStorage`. Receive the descriptors libmxl read from `payload.sock`, in the writer's order. The backend owns them from then on, also when it returns an error, and returns `MXL_ERR_INVALID_STATE` if they do not match the flow. Null when the backend shares none. |
| `describeGeometry` | Before a flow of a media type libmxl does not define is created. Report the planes, slices and slice sizes of a grain of that media type, from the flow definition. Null when the backend defines no media types. |

libmxl passes a `mxlPayloadBackendFlowInfo`: slot count, grain payload size, access mode, plane count, slices per grain, slice sizes, the flow's `mxlDataFormat`, and its definition, from which a backend reads the media type and the frame size. libmxl does not choose which formats a backend may store. The backend rejects a format it cannot hold.

`PluginPayloadStorage` in libmxl is the `PayloadStorage` that forwards these calls. `FlowManager` stores it on the discrete flow with `setPayloadStorage` after `prepare` or `open` returns.

## The cuda-linear backend

`libmxl-payload-cuda-linear.so` is built when `MXL_ENABLE_CUDA_PAYLOAD` is on. It links the static CUDA runtime. libmxl does not link CUDA. The backend stores video flows only. It rejects other formats.

Its storage type is `cuda-device-pointer`. An application includes `mxl/storage/cuda_device_pointer.h`, installed with the backend, which needs MXL's headers and not CUDA's. The header defines what the storage type keeps in the reserved bytes: in the layout, the index in the calling process of the device on which it accesses the payload, and the UUID of the device that holds it, which differ when a reader uses peer access; in each slot description, the slot's device pointer, valid in the calling process. `mxlGetCudaGrainStorageLayout()` and `mxlGetCudaGrainStorage()` copy them out. libmxl's headers know nothing of CUDA.

The writer resolves `deviceUuid` to a CUDA device in its own process, creates one virtual-memory allocation for every slot, aligns each slot to 256 bytes, and exports the allocation once as a POSIX file descriptor. libmxl serves that descriptor on `payload.sock`. A reader in another process imports it in `mapSlots`. A reader in the writer's process shares the allocation through a registry inside the process. The allocation stays alive for that reader after the writer is released.

A reader that passes a different visible `deviceUuid` maps the allocation with peer access when `cudaDeviceCanAccessPeer` allows it. Device writes for a grain must be finished before `mxlFlowWriterCommitGrain`.

Same-node writer and reader pairs share one MXL domain. The domain has to be visible to both, including the socket in the flow directory, as [Sharing descriptors](#sharing-descriptors) describes. They do not need a shared IPC namespace or a shared PID namespace. The reader must see the writer's GPU. A copy between nodes is a separate domain on each node. RDMA from device memory is later work, described in [Grain Payload Storage](PayloadStorage.md).
