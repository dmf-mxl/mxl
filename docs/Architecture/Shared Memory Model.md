<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Shared Memory Model

Flows and Grains are _ring buffers_ stored in memory-mapped files written in a _tmpfs_-backed volume or filesystem. The base folder where flows are stored is called an _MXL domain_. Multiple MXL domains can co-exist on the same host.

## Filesystem Layout

| Path                                                    | Description                                                                                                                   |
|---------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------|
| `${mxlDomain}/`                                         | Base directory of the MXL domain                                                                                              |
| `${mxlDomain}/${flowId}.mxl-flow/`                      | Directory containing resources associated with a flow with uuid `${flowId}`                                                   |
| `${mxlDomain}/${flowId}.mxl-flow/data`                  | Flow header. Contains metadata for a flow ring buffer. Memory mapped by readers and writers.                                  |
| `${mxlDomain}/${flowId}.mxl-flow/flow_def.json`         | NMOS IS-04 Flow resource definition.                                                                                          |
| `${mxlDomain}/${flowId}.mxl-flow/access`                | File 'touched' by readers (if permissions allow it) to notify flow access. Enables reliable 'lastReadTime' metadata update.   |
| `${mxlDomain}/${flowId}.mxl-flow/grains/`               | Directory where individual grains are stored.                                                                                 |
| `${mxlDomain}/${flowId}.mxl-flow/grains/${grainIndex}`  | Grain Header and optional payload (if payload is in host memory and not device memory). Memory mapped by readers and writers. |

!!! note

    FlowWriters will obtain a SHARED advisory lock on any memory-mapped files (data and grains) and hold it until closed. This is used to detect stale flows in the `mxlGarbageCollectFlows()` function (for example, when a crashed media function failed to release the flow properly).

---

The MXL library provides a mechanism for sharing ring buffers of Flow and Grains (see NMOS IS-04 definitions) between media functions executing on the same host (bare-metal or containerized) and/or between media functions executing on hosts connected using fast fabric technologies (RDMA, EFA, etc).

The MXL library provides APIs for zero-overhead grain _sharing_ using a reader/writer model in opposition to a sender/receiver model. With a reader/writer model, no packetization or even memory copy is involved, preserving memory bandwidth and CPU cycles.
