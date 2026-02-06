# OFI Implementation - Bridging the C API to libfabric Internals

## The Translation Layer

This directory bridges two worlds: the clean, flow-oriented C API defined in `fabrics.h` and the raw, low-level libfabric API. It's where MXL's media-centric abstractions meet the hardware-centric reality of RDMA networking.

## The Stub Implementation

**fabrics.cpp** is deliberately minimal. Every public API function returns `MXL_ERR_INTERNAL`. This serves three purposes:

1. **Template for alternative implementations**: Someone building a Fabrics backend for a proprietary transport can start here
2. **Minimal builds**: Projects that don't need remote media exchange can link against this stub, avoiding libfabric dependencies
3. **Clear separation**: The real implementation lives in `internal/`, maintaining a clean architectural boundary

This pattern ensures the Fabrics API is transport-agnostic. Today it's libfabric; tomorrow it could be UCX, Portals, or a custom protocol.

## The Internal Implementation

The `internal/` directory is where the magic happens. It contains ~26 C++ classes that wrap libfabric primitives with RAII semantics, type safety, and exception-based error handling.

### Why C++ Internals Behind a C API?

Libfabric's C API is powerful but dangerous. Resources must be manually allocated and freed, error codes must be checked on every call, and memory management is entirely manual. The internal implementation uses modern C++ to:

- **RAII wrappers**: Automatically close/free resources in destructors (no leaks)
- **Exception-based flow**: Throw on error, simplifying control flow
- **Type-safe variants**: Use `std::variant` for completion/event types instead of raw unions
- **Shared pointers**: Manage object lifetimes across threads

At the API boundary, exceptions are caught and converted to `mxlStatus` codes, preserving C compatibility.

### The libfabric Hierarchy

Libfabric organizes RDMA resources in a strict hierarchy. The internal classes mirror this structure:

```
Fabric (top-level fabric instance)
  |
  +-- Domain (resource container for memory registration)
       |
       +-- Endpoint (communication channel)
       +-- CompletionQueue (data operation completions)
       +-- EventQueue (control event notifications)
       +-- AddressVector (maps remote addresses to handles)
```

Understanding this hierarchy is key to understanding the flow of operations.

## Initialization Flow

### Creating a FabricsInstance

When `mxlFabricsCreateInstance()` is called:

1. **Provider discovery**: The code queries libfabric with `fi_getinfo()` to enumerate available providers (TCP, Verbs, EFA, SHM)
2. **FabricInfoList**: The query returns a linked list of `fi_info` structures, each describing a provider's capabilities
3. **Provider selection**: The implementation filters by the requested provider and endpoint type
4. **Fabric allocation**: The chosen `fi_info` is passed to `fi_fabric()` to create a Fabric object
5. **Domain allocation**: A Domain is created from the Fabric with `fi_domain()`

The Fabric represents the network fabric (physical or virtual), while the Domain provides a namespace for memory registration.

### Setting Up a Target

When `mxlFabricsTargetSetup()` is called:

1. **Endpoint creation**: An Endpoint is allocated with `fi_endpoint()` using the Domain and provider's `fi_info`
2. **Queue allocation**: A CompletionQueue (CQ) is opened with `fi_cq_open()` for data completions
3. **Queue binding**: The Endpoint is bound to the CQ with `fi_ep_bind()`
4. **Address vector (optional)**: For connectionless providers (EFA), an AddressVector is created and bound
5. **Memory registration**: The target's Regions (destination buffers) are registered with `fi_mr_reg()`, yielding MemoryRegion objects containing descriptors and rkeys
6. **Endpoint enable**: The Endpoint is transitioned to active state with `fi_enable()`
7. **Address extraction**: The local fabric address is retrieved with `fi_getname()` and wrapped in a FabricAddress
8. **RemoteRegion generation**: Rkeys and addresses are packaged into RemoteRegion objects
9. **TargetInfo assembly**: FabricAddress and RemoteRegions are bundled into an mxlTargetInfo structure

The TargetInfo is then serialized (using Base64 encoding) and transmitted out-of-band to the initiator.

### Setting Up an Initiator

When `mxlFabricsInitiatorSetup()` is called, the process is similar to the target:

1. **Endpoint creation**: Allocated with `fi_endpoint()`
2. **Queue allocation**: CQ created for tracking outbound operation completions
3. **Queue binding**: Endpoint bound to CQ
4. **Address vector**: AV created and bound (for connectionless providers)
5. **Memory registration**: The initiator's Regions (source buffers) are registered with `fi_mr_reg()`, yielding LocalRegion objects with memory descriptors
6. **Endpoint enable**: Endpoint activated

The initiator doesn't generate TargetInfo; instead, it receives and deserializes TargetInfo from targets.

### Adding a Target

When `mxlFabricsInitiatorAddTarget()` is called:

1. **TargetInfo deserialization**: The base64 string is decoded to extract the target's FabricAddress and RemoteRegions
2. **Address insertion**: The FabricAddress is inserted into the AddressVector with `fi_av_insert()`, returning an `fi_addr_t` handle
3. **Handle storage**: The handle is stored in an internal map, keyed by the target's address

This handle is used in subsequent RDMA write operations to specify the destination.

## Data Transfer Flow

### Initiator: Writing a Grain

When `mxlFabricsInitiatorTransferGrain(grainIndex)` is called:

1. **Region lookup**: The LocalRegion corresponding to `grainIndex` is retrieved (address, length, memory descriptor)
2. **Target iteration**: For each added target, the RemoteRegion (address, length, rkey) is retrieved
3. **RDMA write**: `fi_write()` or `fi_writemsg()` is called with:
   - Local buffer (source): LocalRegion's address and descriptor
   - Remote buffer (destination): RemoteRegion's address and rkey
   - Destination handle: The `fi_addr_t` from the AddressVector
   - Immediate data: The grain index (32-bit value sent inline with the write)

The operation is enqueued in the Endpoint's work queue and returns immediately (non-blocking).

### Initiator: Making Progress

When `mxlFabricsInitiatorMakeProgress*()` is called:

1. **Completion polling**: The implementation calls CompletionQueue's `read()` or `readBlocking(timeout)`
2. **libfabric poll**: Internally, this calls `fi_cq_read()` or `fi_cq_sread()` to retrieve completion entries
3. **Completion parsing**: If a completion is available, it's wrapped in a Completion::Data object (success) or Completion::Error object (failure)
4. **Return status**:
   - `MXL_STATUS_OK` if all operations complete
   - `MXL_ERR_NOT_READY` if operations are still pending

The caller must loop on MakeProgress until all pending operations drain.

### Target: Receiving a Grain

When `mxlFabricsTargetTryNewGrain()` or `mxlFabricsTargetWaitForNewGrain()` is called:

1. **Completion polling**: The CompletionQueue's `read()` or `readBlocking(timeout)` is called
2. **libfabric poll**: Internally, `fi_cq_read()` or `fi_cq_sread()` retrieves a completion entry
3. **Completion type check**: The code checks if this is a remote write completion (`FI_RMA | FI_REMOTE_WRITE` flags)
4. **Immediate data extraction**: The grain index is extracted from the completion's immediate data field
5. **Return**: The grain index is returned to the caller

The target's application can now process the grain at that buffer index.

## Memory Registration Deep Dive

Memory registration is the heart of RDMA. The flow is:

1. **Region construction**: Start with a Region (base address, size, memory location type)
2. **fi_mr_reg()**: Libfabric pins the pages in physical RAM and programs the NIC's DMA engine
3. **MemoryRegion**: The resulting `fid_mr` handle is wrapped in a MemoryRegion object
4. **Descriptor extraction**: `fi_mr_desc()` returns the memory descriptor (used locally in LocalRegion)
5. **Rkey extraction**: `fi_mr_key()` returns the remote key (used remotely in RemoteRegion)
6. **RegisteredRegion**: The MemoryRegion and original Region are bundled
7. **LocalRegion/RemoteRegion**: Generated from RegisteredRegion, depending on role (initiator vs target)

### Virtual vs Offset Addressing

Some providers use virtual addressing (RemoteRegion contains the actual pointer value), while others use offset addressing (RemoteRegion uses 0-based offsets). The Domain's `usingVirtualAddresses()` method determines the mode, and RegisteredRegion's `toRemote()` method generates the correct RemoteRegion accordingly.

## Error Handling Strategy

The internal code uses exceptions for clean error propagation:

- **FabricException**: Wraps a libfabric error code (negative integer like `-FI_ENOMEM`)
- **Exception**: Generic MXL fabrics exception with an `mxlStatus` code

At the C API boundary (in the real implementation, not the stub), exceptions are caught:

```cpp
try {
    // Internal C++ code that may throw
} catch (FabricException const& e) {
    return e.status(); // Convert to mxlStatus
} catch (Exception const& e) {
    return e.status();
} catch (...) {
    return MXL_ERR_INTERNAL;
}
```

The **fiCall()** template in Exception.hpp wraps libfabric calls:

```cpp
fiCall(fi_endpoint, "Failed to create endpoint", domain, info, &ep, nullptr);
```

If the libfabric function returns a negative error code, `fiCall()` throws a FabricException with a descriptive message.

## Asynchronous Operation Model

RDMA is inherently asynchronous. The flow is:

1. **Enqueue**: Operations are posted to the Endpoint's work queue (non-blocking)
2. **Execute**: The NIC's hardware DMA engine executes operations autonomously
3. **Complete**: When done, a completion entry is posted to the CompletionQueue
4. **Poll**: The application polls the CQ to discover completed operations

This decouples submission from completion, enabling pipelining and high throughput.

## Connection Models

Libfabric supports two models:

- **Connection-oriented (MSG)**: Requires explicit connect/accept, similar to TCP. Uses EventQueue for connection events (FI_CONNECTED, FI_SHUTDOWN)
- **Connectionless (RDM/DGRAM)**: No explicit connection. Uses AddressVector to map remote addresses to handles, similar to UDP

MXL Fabrics primarily targets connectionless providers (EFA, SHM in table mode) for flexibility and scalability.

## Serialization Details

TargetInfo is serialized for out-of-band transmission:

1. **FabricAddress**: Binary blob (provider-specific format) encoded to base64 using Base64.hpp
2. **RemoteRegion list**: Array of (address, length, rkey) tuples serialized as structured data (likely JSON or binary protocol)
3. **Combined**: Bundled into a single string or binary blob

The initiator deserializes to reconstruct the target's connection metadata.

## Why This Architecture Works

The separation of concerns is deliberate:

- **C API (fabrics.h)**: High-level, flow-oriented, media-centric
- **Internal C++**: Low-level, resource-oriented, RDMA-centric
- **Stub (fabrics.cpp)**: Minimal, error-returning, dependency-free

This enables:

- Clean API for applications
- Safe resource management internally
- Flexibility to swap backends
- Minimal overhead for non-Fabrics builds

The complexity is hidden behind abstractions, but the performance is exposed to the application.
