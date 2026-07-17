<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# MXL Fabrics — Getting Started

## What is MXL Fabrics?

MXL Fabrics is a library for transferring MXL media flows between hosts over a network. It is built on [libfabric](https://ofiwg.github.io/libfabric/) and supports multiple transport providers, including RDMA Verbs, AWS EFA, TCP, and shared memory.

A transfer involves two sides: a target that exposes its media buffers to the network, and an initiator that writes flow data into those buffers. The target generates a `TargetInfo` object containing its fabric address and memory region keys. This object is shared with the initiator through any out-of-band mechanism (a file, a signaling protocol, etc.). Once the initiator has the target info, it can connect and begin transferring grains (for discrete/video flows) or samples (for continuous/audio flows).

The library uses Remote Write (RDMA) to place data directly into the target's memory without involving the target CPU. The selected provider must support Remote Write.

This guide walks through building the demo tools, discovering network interfaces, and running your first transfer. It assumes familiarity with the MXL Flow API. For the full design rationale and transfer protocol details, see [Fabrics.md](Fabrics.md).

## 1. Building the demo applications

Enable the libfabric-based Fabrics implementation and build:

```bash
cmake --preset Linux-GCC-Release -DMXL_ENABLE_FABRICS_OFI=ON .
cmake --build build/Linux-GCC-Release
```

This produces two binaries:

| Binary | Path |
| - | - |
| `mxl-fabrics-demo` | `build/Linux-GCC-Release/tools/mxl-fabrics-demo/mxl-fabrics-demo` |
| `mxl-fabrics-info` | `build/Linux-GCC-Release/tools/mxl-fabrics-info/mxl-fabrics-info` |

## 2. Discovering interfaces with `mxl-fabrics-info`

`mxl-fabrics-info` lists the interface configurations available on the current host. Each entry is one combination of a provider and a node address.

```bash
mxl-fabrics-info
```

On a machine with an RDMA-capable NIC (`mlx5_0`) and a second non-RDMA NIC (`ens18`), the output might look like this:

```
interface verbs-0 node 10.130.40.12 dev mlx5_0
interface verbs-1 node fe80::8ae9:a4ff:fe48:d514 dev mlx5_0
interface shm-0 node dev03 dev <none>
interface tcp-0 node 10.130.40.12 dev ens16np0
interface tcp-1 node fe80::8ae9:a4ff:fe48:d514 dev ens16np0
interface tcp-2 node 10.130.100.100 dev ens18
interface tcp-3 node fe80::be24:11ff:fe05:3c1d dev ens18
interface tcp-4 node 127.0.0.1 dev lo
interface tcp-5 node ::1 dev lo
```

The same physical interface can appear under multiple providers. Verbs and TCP entries for `10.130.40.12` both refer to the same NIC; the difference is the transport.

### Filtering

Narrow the output by provider (`-p`) and/or node address (`-n`):

```bash
mxl-fabrics-info -p tcp -n ::1
```

```
interface tcp-0 node ::1 dev lo
```

Use `-v` for additional detail about each interface.

## 3. Providers

The library supports several providers. Each provider targets different hardware and connection characteristics:

| Provider | Typical hardware | Endpoint type |
| - | - | - |
| EFA | AWS Elastic Fabric Adapter | RDM (connectionless) |
| Verbs | RDMA NICs (ConnectX, etc.) | MSG (connected) |
| TCP | Any network interface | MSG (connected) |
| SHM | Local (same host) | RDM (connectionless) |

All transfers use Remote Write, which lets the initiator write directly into the target's memory without involving the target CPU. The selected provider must support Remote Write (the `MXL_FABRICS_IFACE_CAP_REMOTE_WRITE` capability flag).

The difference between connected (MSG) and connectionless (RDM) endpoints is abstracted by the library. The API and workflow are the same regardless of the underlying endpoint type, though there can be slight differences in behavior (for example, error reporting on a failed connection-oriented endpoint may differ from a connectionless one).

The library itself does not select a provider or interface automatically. The caller queries the available interfaces with `mxlFabricsGetInterfaces()` and chooses which provider and node address to pass to the setup functions. The demo application (`mxl-fabrics-demo`) implements its own auto-selection heuristic on top of this query; see [section 5.5](#55-interface-auto-selection-in-the-demo-tool) for details.

## 4. Node and service values

The terminology follows libfabric and the Linux Verbs API:

- **node** — typically an IP address assigned to an interface. Use `mxl-fabrics-info` or `fi_info` (from libfabric) to discover available node addresses.
- **service** — for TCP and RoCEv2 this is the port number. Some providers (like EFA) ignore it. The SHM provider concatenates node and service into a filename.

In most cases, let the library choose a random service automatically. Only set it explicitly when you need a fixed, known port.

## 5. Using the demo application

`mxl-fabrics-demo` transfers video (discrete flow) or audio (continuous flow) data between two endpoints. Both target and initiator roles are in the same binary. By default it runs as a target; pass `-i` to run as an initiator.

### 5.1 Command line options

| Option | Description |
| - | - |
| `-d <path>` | MXL domain directory (required). |
| `-f <flow>` | As target: path to a flow definition JSON file. As initiator: the source flow UUID. |
| `--flow-options <path>` | Optional flow options file (target only). |
| `-i` | Run as initiator. Without this flag, the tool runs as target. |
| `-p <provider>` | Use a specific provider (`tcp`, `verbs`, `efa`, `shm`). If omitted, the demo picks one automatically. |
| `-n <address>` | Bind to a specific node address. If omitted, the demo picks one automatically. |
| `-s <service>` | Service identifier (e.g. a port number). |
| `-t <target-info>` | Target info exchange (see below). |

### 5.2 Target info exchange

The target and initiator must exchange a `TargetInfo` object out of band. This object contains the target's fabric address, remote memory keys, and buffer addresses. The demo tool supports two exchange methods:

**File-based** (recommended for local testing): use the `@` prefix to read/write raw JSON.
- Target side: `-t @/tmp/target.json` writes the target info to a file.
- Initiator side: `-t @/tmp/target.json` reads the target info from that file.

**Base64 string**: without the `@` prefix, the target prints a base64-encoded string to stdout, and the initiator accepts the same string via `-t <base64>`.

### 5.3 Target setup

1. Start the target with a flow definition, a domain directory, and optionally a specific provider and node address.
2. The target prints or writes a `TargetInfo`. Share this with the initiator.
3. The target waits for an initiator to connect and then receives grains or samples as they arrive.

### 5.4 Initiator setup

1. Start the initiator with the source flow UUID, the same domain directory, and the target info obtained from the target.
2. The initiator connects and begins transferring grains or samples.

### 5.5 Interface auto-selection in the demo tool

When `-p` and `-n` are omitted, the demo application queries all available interfaces with `mxlFabricsGetInterfaces()` and picks the one whose provider has the highest priority:

| Priority | Provider |
| - | - |
| 4 (highest) | EFA |
| 3 | Verbs |
| 2 | TCP |
| 1 (lowest) | SHM |

This is a convenience built into the demo tool. The Fabrics library itself has no built-in provider preference; your application is responsible for choosing a provider and interface from the list returned by `mxlFabricsGetInterfaces()`.

## 6. Examples

### 6.1 Local TCP transfer

The simplest setup: both endpoints on the same machine using the TCP provider over loopback.

```bash
# Terminal 1 — Target
mxl-fabrics-demo -d /dev/shm -p tcp -n ::1 -s 1312 -f flow.json -t @/tmp/target.json

# Terminal 2 — Initiator
mxl-fabrics-demo -i -d /dev/shm -p tcp -n ::1 -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed -t @/tmp/target.json
```

### 6.2 RDMA Verbs transfer

If there is only one Verbs interface on the system, the demo's auto-selection picks it up without any additional flags:

```bash
# Terminal 1 — Target
mxl-fabrics-demo -d /dev/shm -s 1312 -f flow.json -t @/tmp/target.json

# Terminal 2 — Initiator
mxl-fabrics-demo -i -d /dev/shm -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed -t @/tmp/target.json
```

On a host with multiple Verbs interfaces, use `-n <address>` to select the right one.

### 6.3 EFA transfer

EFA has the highest priority in the demo's auto-selection and will be chosen when available. The service value is ignored by the EFA provider.

```bash
# Terminal 1 — Target
mxl-fabrics-demo -d /dev/shm -f flow.json -t @/tmp/target.json

# Terminal 2 — Initiator
mxl-fabrics-demo -i -d /dev/shm -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed -t @/tmp/target.json
```

## 7. RoCEv2 QoS configuration

When using RDMA over Converged Ethernet (RoCEv2) with Mellanox/NVIDIA NICs and the MLNX_OFED driver, you may want to configure DSCP-based priority and enable Priority Flow Control (PFC). The examples below set DSCP 26, map it to priority 3, and enable PFC for that priority.

In the commands below, `<interface>` is the Linux network interface (e.g. `ens16np0`) and `<ib-interface>` is the InfiniBand device (e.g. `mlx5_0`).

Set the default traffic class for RDMA-CM based RoCE connections (tclass = DSCP * 4):

```bash
cma_roce_tos -d <ib-interface> -t 106
```

Enable DSCP-based QoS trust on the interface:

```bash
mlnx_qos -i <interface> --trust-dscp
```

Map DSCP 26 to priority 3:

```bash
mlnx_qos -i <interface> --dscp2prio set,26,3
```

Enable PFC on priority 3 only:

```bash
mlnx_qos -i <interface> --pfc 0,0,0,1,0,0,0,0
```
