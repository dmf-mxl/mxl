# OFI Internal Implementation - The RDMA Networking Building Blocks

## The Story of Remote Memory Access

This directory tells the story of how RDMA networking works from the ground up. Each file is a character in the narrative of establishing fabric connections, registering memory, and moving data at wire speed with zero copies.

Think of RDMA as a postal system for computer memory. Traditional networking is like mailing a letter: you hand it to the post office (kernel), which processes it through multiple facilities (copies through buffers) before delivering it. RDMA is like having a direct pneumatic tube between buildings: you drop a message in one end, and it arrives at the other instantly, bypassing all intermediaries.

## Part I: Discovery and Configuration

### Provider.hpp / Provider.cpp - The Transport Menu

Every RDMA story begins with a choice: which underlying technology will carry your data? **Provider** enumerates the options:

- **TCP**: Software-based RDMA emulation over standard TCP sockets. Works anywhere but slowest.
- **VERBS**: Hardware RDMA using InfiniBand or RoCE (RDMA over Converged Ethernet). High performance, requires special NICs.
- **EFA**: AWS Elastic Fabric Adapter. Custom RDMA protocol for AWS EC2 instances.
- **SHM**: Shared memory for intra-host communication (testing).

The provider determines hardware requirements, performance characteristics, and available features. This enum maps to libfabric's provider strings ("tcp", "verbs", "efa", "shm") and provides conversion functions for the public API.

### FabricInfo.hpp / FabricInfo.cpp - The Capability Descriptor

Before creating a fabric, you query what's available. **FabricInfo** wraps libfabric's `fi_info` structure, which describes a provider's capabilities:

- Endpoint type (connection-oriented vs connectionless)
- Supported operations (RDMA writes, reads, atomic operations)
- Addressing format (IPv4, IPv6, InfiniBand GID)
- Memory registration modes (virtual vs offset addressing)
- Completion modes (how immediate data is delivered)

Three classes work together:

- **FabricInfo**: Owning RAII wrapper (calls `fi_freeinfo()` in destructor)
- **FabricInfoView**: Non-owning view (safe to pass around without ownership transfer)
- **FabricInfoList**: Owning wrapper for linked lists returned by `fi_getinfo()`

The typical workflow: call `FabricInfoList::get()` to query available configurations, iterate through options, clone the desired `FabricInfo`, and use it to open a Fabric.

### FabricVersion.hpp / FabricVersion.cpp - The API Contract

Libfabric is versioned. **FabricVersion** wraps version numbers and comparison logic, ensuring compatibility between the MXL implementation and the installed libfabric library. Critical for avoiding subtle API mismatches.

## Part II: Building the Fabric Foundation

### Fabric.hpp / Fabric.cpp - The Top-Level Container

**Fabric** is the root resource representing a network fabric instance. In libfabric's hierarchy:

```
Fabric → Domain → Endpoint/CQ/EQ/AV
```

A Fabric encapsulates a specific provider's implementation (TCP, Verbs, EFA, etc.). Key operations:

- **Fabric::open(FabricInfoView)**: Creates a fabric from discovery results using `fi_fabric()`
- **raw()**: Exposes the underlying `fid_fabric*` for libfabric calls
- **Destructor**: Automatically calls `fi_close()` via RAII

The Fabric is the parent for all other resources. It must outlive everything created from it (enforced via `shared_ptr` ownership).

### Domain.hpp / Domain.cpp - The Resource Container

**Domain** groups related RDMA resources together. It's responsible for:

- **Memory registration**: Pins memory pages and creates DMA mappings with `fi_mr_reg()`
- **Resource isolation**: Provides a namespace for endpoints, queues, and address vectors
- **Addressing modes**: Determines whether virtual or offset-based addressing is used
- **Completion modes**: Specifies how completion data (immediate data) is delivered

Key methods:

- **Domain::open(Fabric)**: Creates a domain with `fi_domain()`
- **registerRegions(regions, access)**: Registers a list of memory regions, storing the results internally
- **localRegions()**: Returns LocalRegion objects for use in local RDMA operations
- **remoteRegions()**: Returns RemoteRegion objects to send to remote peers
- **usingVirtualAddresses()**: Queries the addressing mode (affects RemoteRegion generation)
- **usingRecvBufForCqData()**: Determines if the target must post receive buffers for immediate data

The Domain owns all registered memory. When it's destroyed, all registrations are automatically cleaned up.

## Part III: Memory Registration - The Heart of RDMA

### Region.hpp / Region.cpp - Unregistered Memory Descriptors

**Region** represents an unregistered memory buffer. It's simply:

```cpp
struct Region {
    std::uintptr_t base;  // Start address
    std::size_t size;     // Size in bytes
    Location loc;         // Host RAM or device memory (GPU)
};
```

**Region::Location** specifies memory type:

- **host()**: Standard system RAM
- **cuda(deviceId)**: NVIDIA GPU memory (for GPU-Direct RDMA)

Region also provides conversion to `iovec` for libfabric APIs.

**RegionGroup** is a collection of Regions for scatter-gather I/O (vectored operations reading/writing multiple non-contiguous buffers in one call).

**MxlRegions** bridges MXL's flow-based memory model to libfabric regions:

- **mxlRegionsFromFlow(FlowData&)**: Extracts memory regions from MXL flow buffers
- **mxlRegionsFromUser(regions, count)**: Wraps user-provided memory arrays

### MemoryRegion.hpp / MemoryRegion.cpp - Registered Memory Handles

**MemoryRegion** wraps a libfabric memory registration handle (`fid_mr`). The registration process:

1. **Input**: Region (base, size, location) and access flags (FI_WRITE, FI_REMOTE_WRITE, etc.)
2. **Registration**: `fi_mr_reg()` pins pages in physical RAM and programs the NIC's DMA engine
3. **Output**: `fid_mr*` handle containing:
   - **desc**: Memory descriptor for local DMA operations
   - **rkey**: Remote protection key for remote peer access

Key methods:

- **MemoryRegion::reg(Domain&, Region, access)**: Static factory performing registration
- **desc()**: Returns the memory descriptor (void* used in LocalRegion)
- **rkey()**: Returns the remote key (uint64_t used in RemoteRegion)
- **Destructor**: Calls `fi_close(fid_mr)` to unregister

Access flags determine permissions:

- **FI_WRITE**: Local buffer for RDMA writes (initiator)
- **FI_REMOTE_WRITE**: Remote can write to this buffer (target)
- **FI_READ** / **FI_REMOTE_READ**: For RDMA reads (not used in MXL Fabrics)

### RegisteredRegion.hpp / RegisteredRegion.cpp - The Memory Registration Bundle

**RegisteredRegion** combines:

- Original **Region** (base address, size, location)
- **MemoryRegion** (fid_mr handle with desc and rkey)

After registration, it can generate role-specific views:

- **toLocal()**: Creates a LocalRegion for the initiator's source buffers
- **toRemote(useVirtualAddress)**: Creates a RemoteRegion for the target's destination buffers

The addressing mode matters:

- **Virtual addressing**: RemoteRegion contains the actual pointer value (region.base)
- **Offset addressing**: RemoteRegion uses 0-based offset (0)

The mode is determined by the Domain's capabilities (`FI_MR_VIRT_ADDR` flag in `fi_info`).

### LocalRegion.hpp / LocalRegion.cpp - Source Buffers for RDMA

**LocalRegion** represents a local memory buffer used as the source for RDMA operations (initiator side):

```cpp
struct LocalRegion {
    std::uint64_t addr;  // Local virtual address
    std::size_t len;     // Length in bytes
    void* desc;          // Memory descriptor (from fi_mr_desc())
};
```

Used in `fi_write()` calls to specify the source buffer. The `desc` field tells the NIC how to access the memory (contains the registration handle).

**LocalRegionGroup** is a scatter-gather list for vectored I/O, converting to arrays of `iovec` and descriptor pointers for libfabric's `fi_writemsg()`.

### RemoteRegion.hpp / RemoteRegion.cpp - Destination Buffers for RDMA

**RemoteRegion** represents a remote memory buffer used as the destination for RDMA operations (target side):

```cpp
struct RemoteRegion {
    std::uint64_t addr;  // Remote address (virtual or offset-based)
    std::size_t len;     // Length in bytes
    std::uint64_t rkey;  // Remote protection key (from fi_mr_key())
};
```

The rkey is a secret that grants RDMA access. Only share with trusted peers. In MXL, rkeys are transmitted via TargetInfo objects sent out-of-band.

**RemoteRegionGroup** is a collection for scatter-gather writes, converting to `fi_rma_iov` arrays for libfabric RMA operations.

Security note: The rkey is like a password for memory access. Compromising it allows arbitrary writes to the target's memory. MXL assumes secure out-of-band channels for TargetInfo transmission.

## Part IV: Communication Endpoints and Queues

### Endpoint.hpp / Endpoint.cpp - The Communication Channel

**Endpoint** is the workhorse. It's analogous to a socket in traditional networking, but for RDMA. Key lifecycle:

1. **create(Domain)**: Allocates an endpoint with `fi_endpoint()`
2. **bind(EventQueue)**: Binds to EQ for control events (connection management)
3. **bind(CompletionQueue)**: Binds to CQ for data operation completions
4. **bind(AddressVector)**: Binds to AV for addressing (connectionless providers)
5. **enable()**: Transitions to active state with `fi_enable()`
6. **Data transfers**: Now ready for `write()`, `recv()`, etc.

Each endpoint has a unique ID (random `uintptr_t`) embedded in completions and events, enabling multiplexing.

Key data operations:

- **write(LocalRegion, RemoteRegion, destAddr, immData)**: Single-buffer RDMA write
- **write(LocalRegionGroup, RemoteRegion, destAddr, immData)**: Scatter-gather RDMA write
- **recv(LocalRegion)**: Post a receive buffer (if required for immediate data by the provider)

The `destAddr` parameter is an `fi_addr_t` handle from the AddressVector (for connectionless) or `FI_ADDR_UNSPEC` (for connected endpoints).

**immData** is optional 32-bit immediate data sent inline with the write. In MXL, this carries the grain index.

Control operations (for connection-oriented endpoints):

- **connect(FabricAddress)**: Initiate connection to passive endpoint
- **accept()**: Accept incoming connection request
- **shutdown()**: Graceful connection teardown

The Endpoint provides read methods:

- **readQueues()**: Non-blocking poll of CQ and EQ simultaneously
- **readQueuesBlocking(timeout)**: Blocks until completion or event (with 100ms EQ polling interval)

### CompletionQueue.hpp / CompletionQueue.cpp - Data Path Completions

**CompletionQueue** (CQ) is where RDMA operation completions appear. When an RDMA write finishes (locally or remotely), a completion entry is posted.

Key operations:

- **CompletionQueue::open(Domain, attributes)**: Creates CQ with `fi_cq_open()`
- **read()**: Non-blocking poll with `fi_cq_read()` - returns immediately
- **readBlocking(timeout)**: Blocking poll with `fi_cq_sread()` - waits up to timeout

The queue format is `FI_CQ_FORMAT_DATA`, providing:

- **Flags**: Operation type (FI_RMA, FI_WRITE, FI_REMOTE_WRITE)
- **Immediate data**: 64-bit user metadata (optional, requires FI_REMOTE_CQ_DATA capability)
- **Context**: Endpoint identifier (op_context field)

**CompletionQueue** inherits from `enable_shared_from_this` because Completion::Error objects hold `shared_ptr<CompletionQueue>` for `fi_cq_strerror()` calls.

### Completion.hpp / Completion.cpp - Parsed Completion Entries

**Completion** is a type-safe wrapper around raw libfabric completion entries. It uses `std::variant<Data, Error>`:

**Completion::Data** represents success:

- **data()**: Returns optional immediate data (if FI_REMOTE_CQ_DATA flag set)
- **fid()**: Returns endpoint FID
- **isRemoteWrite()**: True if this completion is for an incoming RDMA write (target side)
- **isLocalWrite()**: True if this completion is for an outgoing RDMA write (initiator side)

**Completion::Error** represents failure:

- **toString()**: Human-readable error message from `fi_cq_strerror()`
- **fid()**: Endpoint FID that encountered the error

Usage pattern:

```cpp
auto comp = cq->read();
if (comp && comp->isDataEntry()) {
    auto data = comp->data();
    if (data.isRemoteWrite()) {
        auto grainIndex = data.data(); // Extract immediate data
        // Process received grain at index
    }
}
```

### EventQueue.hpp / EventQueue.cpp - Control Path Events

**EventQueue** (EQ) receives control-plane notifications for connection-oriented endpoints:

- **FI_CONNREQ**: Incoming connection request on passive endpoint
- **FI_CONNECTED**: Connection established (both active and passive sides)
- **FI_SHUTDOWN**: Graceful connection teardown completed

Key operations:

- **EventQueue::open(Fabric, attributes)**: Creates EQ with `fi_eq_open()`
- **read()**: Non-blocking poll with `fi_eq_read()`
- **readBlocking(timeout)**: Blocking poll with `fi_eq_sread()`

**EventQueue** is only needed for connection-oriented (MSG) endpoints. Connectionless (RDM/DGRAM) endpoints typically don't use EQ.

### Event.hpp / Event.cpp - Parsed Event Entries

**Event** wraps raw libfabric event entries using `std::variant`:

- **Event::ConnectionRequested**: Incoming connection request, includes FabricInfo of requester
- **Event::Connected**: Connection established, includes endpoint FID
- **Event::Shutdown**: Connection closed, includes endpoint FID
- **Event::Error**: Control operation failure, includes error code and provider-specific data

Usage pattern:

```cpp
auto event = eq->readBlocking(timeout);
if (event && event->isConnected()) {
    // Connection ready, can start data transfers
}
```

### AddressVector.hpp / AddressVector.cpp - The RDMA Phonebook

**AddressVector** (AV) is essential for connectionless communication. It maps remote fabric addresses to local integer handles (`fi_addr_t`):

1. **Analogy**: AV is like a phonebook. You insert a remote address (phone number), get back a handle (speed-dial number), then use the handle in RDMA operations.

2. **Why?**: Performance and efficiency. The NIC can pre-resolve routing information for handles, avoiding per-operation lookups.

Key operations:

- **AddressVector::open(Domain, attributes)**: Creates AV with `fi_av_open()`
- **insert(FabricAddress)**: Adds remote address via `fi_av_insert()`, returns `fi_addr_t` handle
- **remove(fi_addr_t)**: Removes address from AV
- **addrToString(FabricAddress)**: Converts address to human-readable string (for logging)

**Attributes**:

- **count**: Expected number of addresses (sizing hint for the provider)
- **epPerNode**: Endpoints per remote node (optimization hint)

The AV type is `FI_AV_TABLE`, storing addresses in a table with sequential integer handles.

Usage pattern:

```cpp
auto av = AddressVector::open(domain);
auto remoteAddr = FabricAddress::fromBase64(targetInfoString);
auto handle = av->insert(remoteAddr);
endpoint->write(localRegion, remoteRegion, handle, grainIndex);
```

### Address.hpp / Address.cpp - Fabric Network Identifiers

**FabricAddress** wraps libfabric's opaque binary endpoint addresses. These are analogous to (IP, port) in TCP, but:

- Provider-specific format (not standardized)
- Variable length (can be much larger than IPv6 address)
- Contains fabric-specific routing information

Key operations:

- **FabricAddress::fromFid(fid_t)**: Retrieves local address from endpoint via `fi_getname()`
- **toBase64()**: Serializes to base64 string for out-of-band transmission
- **fromBase64(string)**: Deserializes from base64 string
- **raw()**: Access to raw bytes for insertion into AddressVector

Workflow:

1. Target creates endpoint and extracts address: `auto addr = FabricAddress::fromFid(endpoint)`
2. Target serializes: `std::string encoded = addr.toBase64()`
3. Encoded string sent to initiator (via config file, REST API, signaling channel)
4. Initiator deserializes: `auto remoteAddr = FabricAddress::fromBase64(encoded)`
5. Initiator inserts into AV to enable communication

The base64 encoding ensures binary address data can be safely embedded in text formats like JSON or XML.

## Part V: Utility Classes

### Exception.hpp / Exception.cpp - Error Handling Infrastructure

Two exception classes for clean error propagation:

**Exception**: Base class carrying an `mxlStatus` code:

- **make(status, fmt, args)**: Factory with fmt-style formatting
- **invalidArgument()**, **internal()**, **invalidState()**: Convenience factories for common errors
- **status()**: Returns the `mxlStatus` code
- **what()**: Returns descriptive string (from `std::exception`)

**FabricException**: Extends Exception to include a libfabric error code:

- **make(fiErrno, fmt, args)**: Factory that automatically maps libfabric error to `mxlStatus`
- **fiErrno()**: Returns the original libfabric error code (negative integer)

The **fiCall()** template wraps libfabric API calls:

```cpp
template<typename F, typename... T>
int fiCall(F fun, std::string_view msg, T... args) {
    int result = fun(std::forward<T>(args)...);
    if (result < 0) {
        throw FabricException::make(result, "{}: {}", msg, fi_strerror(result));
    }
    return result;
}
```

Usage:

```cpp
fiCall(fi_endpoint, "Failed to create endpoint", domain, info, &ep, nullptr);
```

If `fi_endpoint()` returns a negative error, `fiCall()` throws with a descriptive message. Otherwise, it returns the result.

At the C API boundary, exceptions are caught and converted:

```cpp
extern "C" mxlStatus mxlFabricsCreateTarget(...) {
    try {
        // Internal C++ code
    } catch (FabricException const& e) {
        return e.status();
    } catch (Exception const& e) {
        return e.status();
    } catch (...) {
        return MXL_ERR_INTERNAL;
    }
}
```

This pattern enables clean RAII and error propagation internally while maintaining C compatibility externally.

### Base64.hpp - Binary Serialization for Out-of-Band Channels

**Base64** is a third-party header-only library (MIT licensed) providing compile-time base64 encoding/decoding. Used by FabricAddress for serialization:

- **base64::encode_into<std::string>(data)**: Encodes binary data to base64 string
- **base64::decode_into<std::vector<uint8_t>>(base64)**: Decodes base64 string to binary

Why base64? Fabric addresses are opaque binary blobs. TargetInfo objects need to be transmitted via text-based protocols (JSON, XML, config files). Base64 encoding makes binary data safe for text formats.

The library uses compile-time lookup tables and template metaprogramming for performance, supporting both little-endian and big-endian architectures.

### Format.hpp - Pretty Printing Utilities

**Format** provides human-readable string representations for debugging and logging. Functions like:

- `formatFabricAddress(addr)`: Converts binary address to hex string
- `formatRemoteRegion(region)`: Shows address, length, rkey in readable format
- `formatCompletion(comp)`: Prints completion details (flags, immediate data, endpoint ID)

Essential for troubleshooting RDMA issues where inspecting raw binary data is impractical.

### VariantUtils.hpp - Type-Safe Variant Helpers

**VariantUtils** provides utilities for working with `std::variant` types used throughout the codebase:

- **Completion**: `std::variant<Data, Error>`
- **Event**: `std::variant<ConnectionRequested, Connected, Shutdown, Error>`

Includes helper functions for visiting variants with overloaded lambdas (the "overloaded pattern"):

```cpp
std::visit(overloaded {
    [](Completion::Data const& data) { /* handle data */ },
    [](Completion::Error const& err) { /* handle error */ }
}, completion);
```

This enables type-safe, exhaustive handling of variant alternatives.

## The Complete RDMA Data Flow

### Target Setup (Receiver)

1. **Provider discovery**: Query available fabrics with `FabricInfoList::get()`
2. **Fabric creation**: `Fabric::open(info)` with chosen provider
3. **Domain creation**: `Domain::open(fabric)`
4. **Memory registration**: `domain->registerRegions(regions, FI_REMOTE_WRITE)`
5. **Endpoint creation**: `Endpoint::create(domain)`
6. **CQ creation**: `CompletionQueue::open(domain)`
7. **AV creation**: `AddressVector::open(domain)` (for connectionless)
8. **Binding**: `endpoint->bind(cq)`, `endpoint->bind(av)`
9. **Enable**: `endpoint->enable()`
10. **Address extraction**: `FabricAddress::fromFid(endpoint->raw())`
11. **RemoteRegion generation**: `domain->remoteRegions()`
12. **TargetInfo assembly**: Package address and remote regions
13. **Serialization**: `address.toBase64()` and serialize remote regions
14. **Transmission**: Send TargetInfo to initiator out-of-band

Target is now ready to receive grains.

### Initiator Setup (Sender)

1. **Provider discovery**: Same as target
2. **Fabric/Domain creation**: Same as target
3. **Memory registration**: `domain->registerRegions(regions, FI_WRITE)`
4. **Endpoint creation**: `Endpoint::create(domain)`
5. **CQ creation**: `CompletionQueue::open(domain)`
6. **AV creation**: `AddressVector::open(domain)`
7. **Binding**: `endpoint->bind(cq)`, `endpoint->bind(av)`
8. **Enable**: `endpoint->enable()`
9. **LocalRegion extraction**: `domain->localRegions()`

Initiator is now ready to add targets and transfer grains.

### Adding a Target

1. **Reception**: Receive serialized TargetInfo from target
2. **Deserialization**: `FabricAddress::fromBase64()` and parse remote regions
3. **AV insertion**: `av->insert(remoteAddress)` returns `fi_addr_t` handle
4. **Storage**: Store handle and remote regions in internal map

Target is now addressable by the initiator.

### Transferring a Grain

1. **Region lookup**: Get LocalRegion for grain index (source buffer)
2. **Target lookup**: Get RemoteRegion and fi_addr_t handle for target
3. **RDMA write**: `endpoint->write(localRegion, remoteRegion, handle, grainIndex)`
4. **Enqueue**: libfabric posts operation to NIC's work queue (non-blocking)
5. **Hardware execution**: NIC's DMA engine reads source buffer, sends to network
6. **Network transport**: Data travels over fabric (InfiniBand, RoCE, EFA, etc.)
7. **Remote NIC**: Destination NIC writes data directly to target's memory
8. **Completion**: Both sides post completions to their CQs

### Initiator: Polling for Completion

1. **Poll**: `cq->read()` or `cq->readBlocking(timeout)`
2. **libfabric query**: `fi_cq_read()` retrieves completion entries
3. **Parsing**: Wrap in `Completion::Data` (success) or `Completion::Error` (failure)
4. **Check**: `completion.isDataEntry()` and `completion.data().isLocalWrite()`
5. **Interpretation**: Local write completion means grain was sent successfully

### Target: Polling for Grain Arrival

1. **Poll**: `cq->read()` or `cq->readBlocking(timeout)`
2. **libfabric query**: `fi_cq_read()` retrieves completion entries
3. **Parsing**: Wrap in `Completion::Data`
4. **Check**: `completion.isDataEntry()` and `completion.data().isRemoteWrite()`
5. **Extract**: `auto grainIndex = *completion.data().data()` (immediate data)
6. **Return**: Grain at buffer index `grainIndex` is ready for processing

## Why This Architecture Is Powerful

### Zero-Copy Efficiency

Once memory is registered, RDMA writes involve:

- **Zero CPU copies**: NIC directly accesses application buffers
- **Zero system calls**: Enqueue and poll are userspace operations
- **Zero kernel involvement**: Data path bypasses the OS entirely

Result: Sub-microsecond latencies and multi-gigabyte-per-second throughput.

### Hardware Offload

The NIC's DMA engine autonomously:

- Reads source buffers
- Packetizes data
- Transmits over network
- Writes to destination buffers
- Posts completions

The CPU is free to do other work (or sleep) while data moves.

### Memory Safety via Registration

Registration isn't just for performance. It's a security boundary:

- **Pinning**: Prevents pages from being swapped or moved
- **DMA mapping**: Creates physical address mappings for the NIC
- **Protection**: Rkeys act as passwords, preventing unauthorized access

Without registration, the NIC could access invalid memory or pages could move during DMA, causing corruption.

### Scalability via AddressVector

The AV enables one-to-many communication:

- An initiator inserts N target addresses into its AV
- Each RDMA write specifies a target handle
- The same source buffer can be written to multiple targets
- Hardware multicast (provider-dependent) can optimize this further

Result: Efficient video distribution (one sender, many receivers).

### Asynchronous Pipelining

Decoupling submission from completion enables pipelining:

1. Enqueue transfer 1 (returns immediately)
2. Enqueue transfer 2 (returns immediately)
3. Enqueue transfer 3 (returns immediately)
4. Poll completions (batched)

The NIC executes operations in parallel or in a pipelined fashion, maximizing throughput.

### Clean Abstraction Layers

The class hierarchy mirrors libfabric's resource hierarchy, but adds:

- **RAII**: Automatic cleanup (no leaks)
- **Type safety**: Compile-time checks (no raw pointers)
- **Exception safety**: Clean error propagation (no error code spaghetti)

The result is robust code that's easier to maintain and extend.

## The RDMA Promise

Traditional networking stacks evolved to multiplex limited bandwidth across many flows, adding layers of abstraction and buffering. RDMA inverts this: assume abundant bandwidth, minimize latency, and eliminate intermediaries.

The result is a networking model that looks more like memory access than I/O. RDMA writes complete in microseconds with nanosecond jitter, enabling real-time workflows that were previously impossible:

- Uncompressed 4K video at 120fps between data centers
- Low-latency audio processing distributed across racks
- Zero-copy ingest from cameras directly to GPU memory

This directory implements that promise. Every class, every method, every design decision serves the goal: move media at wire speed with zero copies and minimal latency. The complexity is hidden behind abstractions, but the performance is exposed to the application.

Welcome to the future of media networking.
