# MXL Fabrics Layer - Extending Media Exchange to Remote Hosts

## The Big Picture

MXL's core strength is zero-copy shared-memory media exchange between processes on a single machine. The Fabrics layer extends this power across the network, enabling media grains to flow between machines with the same zero-copy efficiency using RDMA (Remote Direct Memory Access) technology.

Think of it this way: MXL handles the "local neighborhood" where processes share memory, and Fabrics is the "highway system" that connects different neighborhoods across data centers, cloud regions, or continents.

## Why Fabrics Matters

Traditional network protocols (TCP/IP) involve the CPU copying data multiple times through kernel buffers. For high-resolution media (4K video at 60fps, uncompressed audio), this becomes a bottleneck. RDMA lets network hardware directly access application memory, bypassing the CPU entirely. The result? Dramatically lower latency and CPU usage, with gigabytes-per-second throughput.

The Fabrics layer abstracts the complexity of RDMA behind a clean C API that mirrors MXL's local semantics.

## Core Architecture

### The API Layer

The public interface lives in **include/mxl/fabrics.h**. This header defines the complete C API for remote media exchange. It establishes the conceptual model:

- **FabricsInstance**: The root object managing all RDMA resources
- **Target**: A receiver endpoint waiting for incoming media grains
- **Initiator**: A sender endpoint pushing grains to one or more targets
- **Regions**: Memory buffers registered with the network hardware for zero-copy access
- **TargetInfo**: Connection metadata (address, memory keys) transmitted out-of-band from target to initiator

The API workflow is deliberately asymmetric:

1. The **target** sets up first, registering its receive buffers and generating a TargetInfo object
2. TargetInfo is serialized (to base64 string) and transmitted via an out-of-band channel (config file, REST API, signaling protocol)
3. The **initiator** deserializes the TargetInfo and adds it to its target list
4. The initiator enqueues grain transfers and polls for completion
5. The target polls its completion queue to detect newly arrived grains

This model supports one-to-many multicast: an initiator can push the same grain to multiple targets simultaneously.

### Provider Selection

Fabrics supports multiple transport implementations called "providers":

- **TCP**: Software RDMA emulation over standard sockets (universally compatible, slowest)
- **VERBS**: Hardware RDMA using InfiniBand or RoCE networks (highest performance, requires special NICs)
- **EFA**: AWS Elastic Fabric Adapter (custom RDMA for AWS EC2 instances with EFA support)
- **SHM**: Shared memory for testing intra-host scenarios

The provider determines performance characteristics, hardware requirements, and feature availability. Users specify the provider when creating a FabricsInstance.

### Memory Registration

Before RDMA can occur, memory must be "pinned" (prevented from being paged to disk) and registered with the network hardware. The API provides two paths:

- **mxlFabricsRegionsForFlowReader/Writer()**: Extract memory regions from MXL flow objects (the typical path)
- **mxlFabricsRegionsFromUserBuffers()**: Register arbitrary user-provided buffers

Registration yields memory descriptors (for local operations) and remote keys (rkeys, for remote peer access). The target's rkeys are embedded in TargetInfo and transmitted to the initiator.

## Implementation Strategy

The **ofi/** subdirectory contains the concrete implementation using libfabric (OpenFabrics Interface), the industry-standard RDMA API. The stub **ofi/src/fabrics.cpp** returns errors for all functions, serving as a template for alternative implementations or minimal builds without fabrics support.

The real implementation resides in **ofi/src/internal/**, a collection of C++ wrappers around libfabric primitives. These classes provide RAII semantics, type safety, and exception-based error handling, which is converted to C status codes at the API boundary.

## Data Flow Overview

### On the Target (Receiver)

1. Create FabricsInstance from an mxlInstance
2. Create a Target endpoint
3. Extract Regions from a FlowWriter (destination buffers for incoming grains)
4. Call mxlFabricsTargetSetup() with bind address, provider, and regions
5. Serialize the returned TargetInfo and transmit it out-of-band to the initiator
6. Poll with mxlFabricsTargetTryNewGrain() or mxlFabricsTargetWaitForNewGrain()
7. When a grain arrives, the grain index is returned via immediate data sent with the RDMA write

### On the Initiator (Sender)

1. Create FabricsInstance from an mxlInstance
2. Create an Initiator endpoint
3. Extract Regions from a FlowReader (source buffers containing grains to send)
4. Call mxlFabricsInitiatorSetup() with bind address, provider, and regions
5. Receive serialized TargetInfo from target (via out-of-band channel)
6. Deserialize and add target with mxlFabricsInitiatorAddTarget()
7. Enqueue grain transfers with mxlFabricsInitiatorTransferGrain(grainIndex)
8. Pump the network with mxlFabricsInitiatorMakeProgress*() to execute pending operations

The initiator can add/remove multiple targets dynamically, multicasting the same grain to all active targets.

## Threading Model

Each target and initiator operates independently. They can be polled from different threads, but synchronization is the caller's responsibility if objects are shared across threads.

## Helper Functions

The API includes serialization helpers for out-of-band communication:

- **mxlFabricsTargetInfoToString()**: Serialize TargetInfo to base64 string
- **mxlFabricsTargetInfoFromString()**: Deserialize TargetInfo from base64 string
- **mxlFabricsProviderToString()** / **mxlFabricsProviderFromString()**: Provider enum conversion

These enable integration with configuration files (JSON, YAML), REST APIs, or custom signaling protocols.

## What Makes This Different

Traditional network APIs (Berkeley sockets) are byte-stream oriented and CPU-intensive. Fabrics is:

- **Zero-copy**: Network hardware directly accesses application buffers
- **Kernel-bypass**: No system calls in the data path after setup
- **Hardware-offloaded**: NIC handles protocol processing, freeing the CPU
- **Media-aware**: Grain-oriented API matches media workflows

The result is sub-microsecond latencies and multi-gigabyte-per-second throughput with minimal CPU load, enabling real-time media workflows across network boundaries that were previously impossible.
