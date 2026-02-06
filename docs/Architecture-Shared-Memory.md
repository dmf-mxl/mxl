<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Shared Memory Model

Flows and Grains are _ring buffers_ stored in memory mapped files written in a _tmpfs_ backed volume or filesystem. The base folder where flows are stored is called an _MXL domain_. Multiple MXL domains can co-exist on the same host.

## Visual explanation

The shared memory model operates by mapping filesystem entries directly into the address space of both readers and writers. This eliminates the need for data copies or serialization.

```
┌─────────────────────────────────────────────────────────────────┐
│                         MXL Domain                              │
│                     (/dev/shm/mxl)                              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │
          ┌───────────────────┴────────────────────┐
          │                                        │
          ▼                                        ▼
┌──────────────────────┐                ┌──────────────────────┐
│   Flow Writer        │                │   Flow Reader        │
│   (Process A)        │                │   (Process B)        │
│                      │                │                      │
│  mmap(PROT_WRITE)    │                │  mmap(PROT_READ)     │
│                      │                │                      │
│  Writes grains to    │                │  Reads grains from   │
│  ring buffer indices │                │  ring buffer indices │
└──────────────────────┘                └──────────────────────┘
          │                                        │
          └────────────────┬───────────────────────┘
                           │
                           ▼
                  ┌─────────────────┐
                  │  Shared Memory  │
                  │   Ring Buffer   │
                  │                 │
                  │  [Grain 0]      │
                  │  [Grain 1]      │
                  │  [Grain 2]      │
                  │     ...         │
                  │  [Grain N-1]    │
                  └─────────────────┘
```

Both the writer and reader operate on the same physical memory pages. Synchronization is achieved through futexes embedded in the shared memory headers, not through traditional IPC mechanisms.

## Filesystem layout

| Path                                                    | Description                                                                                                                   |
|---------------------------------------------------------| ----------------------------------------------------------------------------------------------------------------------------- |
| \${mxlDomain}/                                          | Base directory of the MXL domain                                                                                              |
| \${mxlDomain}/\${flowId}.mxl-flow/                      | Directory containing resources associated with a flow with uuid ${flowId}                                                     |
| \${mxlDomain}/\${flowId}.mxl-flow/data                  | Flow header. contains metadata for a flow ring buffer. Memory mapped by readers and writers.                                  |
| \${mxlDomain}/\${flowId}.mxl-flow/flow_def.json         | NMOS IS-04 Flow resource definition.                                                                                          |
| \${mxlDomain}/\${flowId}.mxl-flow/access                | File 'touched' by readers (if permissions allow it) to notify flow access. Enables reliable 'lastReadTime' metadata update.   |
| \${mxlDomain}/\${flowId}.mxl-flow/grains/               | Directory where individual grains are stored.                                                                                 |
| \${mxlDomain}/\${flowId}.mxl-flow/grains/\${grainIndex} | Grain Header and optional payload (if payload is in host memory and not device memory ). Memory mapped by readers and writers |
| \${mxlDomain}/\${flowId}.mxl-flow/channels              | (Continuous flows only) Shared memory blob containing all channel buffers for audio flows                                     |

## How filesystem layout maps to the data model

The filesystem layout is designed for efficiency and clarity:

1. **Flow-level isolation**: Each flow has its own directory, enabling per-flow permission management and cleanup.
2. **Grain indexing**: Discrete flows store grains as individual files named by their ring buffer index. This allows the OS page cache to manage memory pressure efficiently.
3. **Continuous flow buffers**: Audio flows use a single `channels` file containing all channel ring buffers laid out sequentially in memory.
4. **Metadata separation**: `data` contains the mutable flow metadata (head index, timestamps), while `flow_def.json` contains immutable configuration.
5. **Discovery**: Tools can enumerate flows by listing `*.mxl-flow` directories in the domain.

For detailed internal data structures, see:
- `lib/include/mxl/SUMMARY.md` for public API documentation
- `lib/internal/SUMMARY.md` for internal implementation details

## Note on advisory locks

FlowWriters will obtain a SHARED advisory lock on any memory mapped files (data and grains) and hold it until closed. This is used to detect stale flows in the _mxlGarbageCollectFlows()_ function (for example, when a crashed media function failed to release the flow properly)

---

[Back to Architecture overview](./Architecture.md)
