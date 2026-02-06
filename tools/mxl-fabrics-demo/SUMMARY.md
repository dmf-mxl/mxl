# mxl-fabrics-demo: RDMA Transport Over Network Fabrics

This tool demonstrates MXL's most advanced capability: zero-copy network transport of media flows using RDMA (Remote Direct Memory Access). When shared memory isn't enough and you need to move flows across network boundaries, this is your gateway to understanding MXL's fabrics layer.

## The Story

MXL flows naturally live in shared memory, accessible only within a single host. But professional media workflows span multiple machines, racks, and even data centers. The fabrics layer extends MXL's zero-copy philosophy across the network using libfabric/OFI, supporting everything from simple TCP sockets to high-performance InfiniBand and AWS EFA.

The tool operates in two distinct modes: target (receiver) and initiator (sender). This client-server architecture reflects the underlying RDMA model, where one side exposes memory regions and the other writes into them directly, bypassing the operating system's network stack.

## The Architecture: demo.cpp

The entire demonstration lives in **demo.cpp**, a carefully structured application that shows both sides of the RDMA conversation.

### Configuration and Setup

The `Config` struct encapsulates everything needed to run in either mode: the MXL domain path, flow identification (UUID for initiator, NMOS JSON path for target), network endpoint parameters (node and service identifiers), and the fabric provider selection (TCP, InfiniBand Verbs, or AWS EFA).

The global `g_exit_requested` flag, set by the signal handler, enables graceful shutdown. When SIGINT or SIGTERM arrives, both modes complete their current operations and clean up resources properly rather than terminating abruptly.

## Target Mode: The Receiver

The `AppTarget` class implements the receiver side. This is where flows are created and written by remote initiators.

### Setup Phase

During setup, the target:

1. Creates an MXL instance and flow writer from the provided NMOS JSON descriptor
2. Creates a fabrics instance (the MXL-fabrics bridge)
3. Obtains memory regions from the flow writer using `mxlFabricsRegionsForFlowWriter()`
4. Creates and configures a fabrics target with network parameters
5. Generates target info - a serialized structure containing memory region addresses, keys, and network endpoints

The target info is printed as a base64-encoded string. This is the critical piece of data the initiator needs to connect. Think of it as a remote memory map and connection ticket rolled into one.

### The Reception Loop

Once running, the target enters `AppTarget::run()`, where it:

- Calls `mxlFabricsTargetWaitForNewGrain()` to block until a remote initiator completes writing a grain via RDMA
- Opens the grain locally (without modifying the payload, as the remote side already wrote it)
- Commits the grain to update the flow's head index, making the data available to local readers
- Logs grain details (index, valid slices, total slices, size)

The beauty of this design: the target never touches the payload data. The remote initiator writes directly into the target's memory using RDMA, and the target simply acknowledges completion by committing the grain. This is true zero-copy networking.

### Error Handling

The target handles several timing conditions:

- **MXL_ERR_TIMEOUT**: No completion within the timeout period, suggesting upstream problems
- **MXL_ERR_INTERRUPTED**: Signal received, triggering graceful shutdown
- **Other errors**: Fatal conditions logged and returned

## Initiator Mode: The Sender

The `AppInitator` class implements the sender side, which reads from local flows and transmits them to remote targets.

### Setup Phase

The initiator's setup is symmetric to the target:

1. Creates an MXL instance and flow reader for the specified flow UUID
2. Creates a fabrics instance
3. Obtains memory regions from the flow reader using `mxlFabricsRegionsForFlowReader()`
4. Creates and configures a fabrics initiator with network parameters
5. Parses the target info string (passed via command line)
6. Adds the target, triggering connection establishment

The call to `mxlFabricsInitiatorMakeProgressBlocking()` in a loop allows the connection state machine to advance. This is libfabric's event-driven model: you provide a timeout, and the function returns when progress occurs or the timeout expires. The status MXL_ERR_NOT_READY means "still working on it, call me again."

### The Transmission Loop

Once connected, `AppInitator::run()` enters the main transfer loop:

- Calculates the current grain index based on the flow's grain rate
- Calls `mxlFlowReaderGetGrain()` to wait for the grain to become available locally
- Transfers the grain via `mxlFabricsInitiatorTransferGrain()`, which programs the RDMA operation
- Calls `mxlFabricsInitiatorMakeProgressBlocking()` to wait for transfer completion
- Handles partial commits (when `validSlices != totalSlices`) by processing the same grain again

The error handling mirrors typical MXL reader patterns:

- **MXL_ERR_OUT_OF_RANGE_TOO_LATE**: Local data expired, fast-forward to current time
- **MXL_ERR_OUT_OF_RANGE_TOO_EARLY**: Data not ready yet, retry with same index
- **Other errors**: Log and retry, as production systems often experience transient failures

### Graceful Disconnect

When `g_exit_requested` is set, the initiator calls `mxlFabricsInitiatorRemoveTarget()` and polls for disconnection completion. This ensures clean teardown rather than abruptly closing the connection, allowing the target to detect the disconnect and clean up its state.

## The Two-Step Workflow

Running the demo requires coordination between two instances:

**Step 1: Start the Target**
```bash
./mxl-fabrics-demo -d /tmp/domain -f flow.json \
  --node 2.2.2.2 --service 1234 --provider verbs
```

The target starts, creates the flow writer, sets up the RDMA endpoint, and prints a long base64 string. Copy this string.

**Step 2: Start the Initiator**
```bash
./mxl-fabrics-demo -i -d /tmp/domain -f <flow-uuid> \
  --node 1.1.1.1 --service 1234 --provider verbs \
  --target-info <base64-string-from-step-1>
```

The initiator parses the target info, connects to the target, and begins transferring grains. From this point, media flows from the initiator's MXL domain to the target's MXL domain with minimal CPU involvement and zero copies on the network path.

## Network Topology Considerations

The `--node` and `--service` parameters map to libfabric's addressing model:

- **node**: The fabric interface identifier. This can be an IP address (for TCP), an interface name (for Verbs), or a device identifier (for EFA).
- **service**: A service identifier, analogous to a port number in TCP/IP terminology.
- **provider**: Determines which libfabric backend to use. Options include tcp (works everywhere but slowest), verbs (InfiniBand, high performance), and efa (AWS Elastic Fabric Adapter).

Different providers have different performance characteristics and requirements. TCP works over standard Ethernet, requires no special hardware, but involves more CPU overhead. Verbs requires InfiniBand or RoCE adapters but delivers true RDMA performance. EFA is AWS-specific, designed for cloud environments where traditional RDMA isn't available.

## Memory Region Management

Both modes call region registration functions (`mxlFabricsRegionsForFlowReader()` and `mxlFabricsRegionsForFlowWriter()`). These functions return `mxlRegions`, structures describing the memory areas involved in the flow. The fabrics layer registers these regions with the RDMA hardware, obtaining memory keys that permit remote access. This registration is a one-time cost paid during setup, after which zero-copy transfers proceed at wire speed.

The target must call `mxlFabricsRegionsFree()` after setup to release the registration resources, as these are no longer needed once the target info is generated.

## What This Tool Teaches

The mxl-fabrics-demo reveals:

- How to extend MXL flows across network boundaries
- The initiator/target model for RDMA-based media transport
- Proper lifecycle management for fabrics instances, initiators, and targets
- Connection establishment and teardown patterns
- The relationship between flow memory regions and RDMA registrations
- Error handling strategies for network transport
- The role of MakeProgress functions in event-driven libfabric programming
- How target info encapsulates connection details and memory descriptors

This isn't a toy. With appropriate network infrastructure (high-speed Ethernet, InfiniBand, or cloud fabric services), this pattern scales to real-time uncompressed video transport, multi-channel audio distribution, and synchronized multi-flow workflows spanning multiple facilities. The demo shows you the primitives; your application combines them into sophisticated distributed media pipelines.

The code is production-quality, with comprehensive error handling, graceful shutdown support, and clear separation between setup, operation, and teardown phases. Use it as a template for building fabric-aware media applications that need to move MXL flows beyond a single host's shared memory.
