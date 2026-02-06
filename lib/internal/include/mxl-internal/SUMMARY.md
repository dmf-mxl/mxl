# The Internal Architecture: MXL's Engine Room

This directory contains the **internal implementation** of the MXL SDK -- the engine that turns the clean public C API into reality using POSIX mmap, futex, advisory locks, and careful shared-memory choreography. Applications never see these headers directly. This is where the magic happens.

## The Story

Picture the public API as a control panel -- clean buttons labeled "create flow writer," "get grain," "wait for data." Behind that panel is a Rube Goldberg machine of memory-mapped files, ring-buffer arithmetic, futex waits, JSON parsing, and garbage collection. This directory is that machine.

The architecture follows a **layered design**:

1. **Primitives** (bottom layer): Platform abstractions for shared memory, synchronization, time, threading, logging.
2. **Data structures** (middle layer): C++ wrappers for the C structures in `flowinfo.h`, plus internal state tracking.
3. **Factories and managers** (orchestration layer): Objects that create/open/delete flows, parse JSON, map files.
4. **Readers and writers** (top layer): The concrete implementations of the opaque handles returned by the public API.

Let's walk through each layer, file by file, and see how it all fits together.

---

## The Primitives Layer: Foundation for Zero-Copy

These files abstract away POSIX-specific details and provide building blocks for everything above.

### `SharedMemory.hpp` -- The Memory Mapping Engine

This is the **core of MXL's zero-copy architecture**. Every flow, every grain, every sample buffer lives in a memory-mapped file (usually on tmpfs).

#### **Why memory-mapped files?**

Traditional IPC (pipes, message queues, sockets) involves copying bytes from one process's address space to another. MXL avoids this entirely: both the writer and reader `mmap()` the same file, so they share physical memory pages. The writer writes pixels directly into shared memory; the reader reads them from the same location. Zero copies. The kernel just updates page tables.

#### **Key design choices:**

- **Regular files on tmpfs** (not `shm_open`): Allows hierarchical organization (`domain/flowId.mxl-flow/grains/data.0`), easier garbage collection (just `rm` stale files), works with existing filesystem permissions.

- **Advisory file locks** (`fcntl` with `F_SETLK`): FlowWriters hold shared or exclusive locks on the `data` file. Used for garbage collection: you can only delete flows with no active locks. **NOT used for synchronization** -- futexes handle that.

- **Read-only vs. read-write mappings**: FlowReaders typically use `PROT_READ` (can't accidentally corrupt data). FlowWriters use `PROT_READ | PROT_WRITE`. Crucially, readers can still use futexes on `PROT_READ` memory because futex doesn't require write permissions.

- **Lazy allocation**: Files are created with `ftruncate()` to the desired size. Physical pages are allocated on first write (sparse files). Keeps memory usage low for inactive flows.

- **Lock upgrade**: `SharedMemoryBase::makeExclusive()` upgrades a shared lock to exclusive (non-blocking). Used when a FlowWriter needs exclusive access to update flow state. Returns `false` if another writer holds a shared lock.

#### **Classes:**

- **`SharedMemoryBase`**: Base class managing file descriptor, `mmap()` pointer, advisory lock, access mode tracking.
- **`SharedMemoryInstance<T>`**: Templated derived class providing type-safe access to the mapped memory. Returns `T*` pointers to the structure stored in the file.

#### **Lifecycle:**

1. Constructor opens/creates file, calls `mmap()`, acquires advisory lock.
2. Accessors provide `T*` pointers into the mapped region (zero-copy).
3. Destructor releases lock, calls `munmap()`, closes file descriptor.

This is the bedrock. Everything else in MXL is built on top of these shared memory mappings.

---

### `Sync.hpp` -- Futex-Based Signaling

MXL uses **Linux futex** (fast userspace mutex) for cross-process synchronization. This is critical because traditional POSIX mutexes require `PROT_WRITE`, which conflicts with readers' `PROT_READ` mappings. Futexes work on any memory address, even read-only pages.

#### **How it works:**

1. FlowWriter updates a counter (e.g., `syncCounter`, `headIndex`) in shared memory.
2. FlowWriter calls `wakeAll(&counter)` to signal readers.
3. FlowReaders use `waitUntilChanged(&counter, expectedValue, deadline)` to sleep until the counter changes.
4. Futex checks if the value still matches `expectedValue`. If not, it returns immediately (no kernel call). If it matches, the kernel puts the thread to sleep until `wakeAll()` is called.

This allows readers with `PROT_READ` mappings to efficiently wait for writer updates without polling or requiring write permissions.

#### **Functions:**

- **`waitUntilChanged(addr, expected, deadline)`**: Blocks until `*addr != expected` or deadline expires. Returns `true` if changed, `false` on timeout.
- **`wakeOne(addr)`**: Wakes a single thread waiting on `addr` (rarely used).
- **`wakeAll(addr)`**: Wakes all threads waiting on `addr` (typical usage after committing grains/samples).

#### **Typical usage:**

```cpp
// Reader loop
while (!haveNewGrain) {
    auto oldCount = flowState->syncCounter;
    if (!waitUntilChanged(&flowState->syncCounter, oldCount, deadline)) {
        return MXL_ERR_TIMEOUT;
    }
    // Check if new grain is now available
}

// Writer commit
flowState->headIndex = newIndex;
flowState->syncCounter++;
wakeAll(&flowState->syncCounter);  // Wake all readers
```

Futex is what makes MXL's waits efficient. No spinning, no polling, no busy-waiting. Just sleep until the writer tells you there's new data.

---

### `Time.hpp` and `Timing.hpp` -- TAI Time and Duration Math

MXL's timing model is built on **TAI** (International Atomic Time, SMPTE ST 2059 epoch). Every timestamp is expressed as nanoseconds since the TAI epoch.

- **`Time.hpp`**: C++ wrappers for the public `time.h` functions. Provides `getTAI()` (read system clock), `sleep()`, timestamp-to-index conversions.

- **`Timing.hpp`**: Internal types for time math:
  - **`Timepoint`**: A specific instant in time (nanoseconds since epoch).
  - **`Duration`**: A span of time (nanoseconds).
  - **`Clock::Realtime`** and **`Clock::TAI`**: Abstractions over `CLOCK_REALTIME` / `CLOCK_TAI`.

- **`Rational.hpp`**: C++ wrapper for `mxlRational`, providing arithmetic operators and conversions. Used throughout for frame/sample rate math.

- **`IndexConversion.hpp`**: Functions to convert between grain/sample indices and TAI timestamps using rational edit rates. Ensures exact integer arithmetic (no floating point).

- **`detail/ClockHelpers.hpp`**: Low-level clock utilities (getting epoch offsets, adjusting for leap seconds, etc.).

---

### `Thread.hpp` -- Threading Abstractions

Provides POSIX pthread wrappers for platforms that need them (not heavily used in current MXL -- most threading is left to applications). Future-proofing for platform-specific threading models.

---

### `Logging.hpp` -- Debug Output

Internal logging macros and functions. Controlled by environment variables or build flags. Used throughout for debugging flow creation, reader/writer lifecycle, synchronization events.

---

### `PathUtils.hpp` and `MediaUtils.hpp` -- Utilities

- **`PathUtils.hpp`**: Functions to construct flow directory paths, grain file paths, channel buffer paths. Converts UUIDs to strings, sanitizes paths.

- **`MediaUtils.hpp`**: Media-specific calculations:
  - V210 line length (SMPTE 425 padding)
  - Slice counts for video (interlaced vs. progressive)
  - Sample buffer sizes for audio
  - Planar vs. interleaved layout helpers

---

## The Data Structures Layer: Internal Memory Layouts

These files define the **actual structures stored in shared memory**, layered on top of the public `mxl/flowinfo.h` definitions.

### `FlowState.hpp` -- Internal Flow Synchronization State

Extends `mxlFlowRuntimeInfo` with fields needed for internal operation:

- **`inode`**: The inode number of the `data` file at creation time. Readers check this on every access to detect if the flow was deleted and recreated (stale flow detection).

- **`syncCounter`**: A monotonically increasing counter used as the futex address. Writers increment this and call `wakeAll(&syncCounter)` after every commit. Readers wait on this counter.

This structure lives inside the `Flow` structure in the `data` file, right after `mxlFlowInfo`.

---

### `Flow.hpp` -- Shared Memory Layout

Defines the structures stored in flow and grain files:

- **`Flow`**: The top-level structure in the `data` file.
  - `mxlFlowInfo info` (public API structure: UUID, format, rate, ring buffer params)
  - `FlowState state` (internal sync state: inode, `syncCounter`)
  - Derived classes (`DiscreteFlowState`, `ContinuousFlowState`) extend this with flow-type-specific fields.

- **`GrainHeader`**: The fixed 8192-byte header at the start of each grain file.
  - `mxlGrainInfo info` (public API structure: index, size, slices, flags)
  - Padding to 8192 bytes (room for future expansion or user metadata)

- **`Grain`**: The complete grain structure.
  - `GrainHeader header` (8192 bytes)
  - Payload follows immediately (video pixels, ancillary data, etc.)

- **`MXL_GRAIN_PAYLOAD_OFFSET`**: Constant defining payload offset (8192). Ensures page alignment and AVX-512 alignment for SIMD operations on payload.

#### **Why 8192-byte grain header?**

- 2x typical page size (4096) for page-aligned payload.
- AVX-512 alignment (64 bytes) -- enables SIMD operations on payload without unaligned access penalties.
- Room for future expansion without breaking ABI.
- Allows embedding user metadata in grain header.

#### **Versioning:**

- `FLOW_DATA_VERSION` (currently 1): Readers check this and reject incompatible versions.
- `GRAIN_HEADER_VERSION` (currently 1): Same for grain headers.

---

### `FlowInfo.hpp` -- C++ Wrappers for Public API Structures

Provides C++ convenience functions and operators for `mxlFlowInfo`, `mxlFlowConfigInfo`, `mxlFlowRuntimeInfo`. Stream operators for debugging, equality comparisons, field accessors.

---

### `FlowData.hpp` -- Base Class for Flow Shared Memory Management

Abstract base class that encapsulates the `data` file mapping and provides accessors.

#### **Hierarchy:**

```
FlowData (abstract base)
  - Manages SharedMemoryInstance<Flow> (the "data" file)
  - Provides accessors: flow(), flowInfo(), flowState()
  |
  +-- DiscreteFlowData (for VIDEO/DATA)
  |     - Manages vector of grain file mappings (lazy)
  |     - Provides grain-specific operations
  |
  +-- ContinuousFlowData (for AUDIO)
        - Manages "channels" file mapping (sample ring buffer)
        - Provides sample-specific operations
```

#### **Key responsibilities:**

- Map the flow's `data` file into shared memory (via `SharedMemoryInstance<Flow>`).
- Provide type-safe access to `Flow` structure fields.
- Manage advisory file lock (for GC coordination).
- Track whether this instance created the flow (for initialization).

#### **Lifecycle:**

1. Constructor maps `data` file (or creates it if mode is `CREATE_READ_WRITE`).
2. If `created()` returns `true`, derived class initializes flow-specific fields.
3. Accessors provide const/non-const access to shared memory.
4. Destructor unmaps and releases lock (via `SharedMemoryInstance` destructor).

---

### `DiscreteFlowData.hpp` -- Grain Ring Buffer Management

Specialization of `FlowData` for discrete flows (VIDEO/DATA).

#### **Filesystem layout:**

```
${domain}/${flowId}.mxl-flow/
  data               -- DiscreteFlowState (ring buffer metadata)
  grains/
    data.0           -- Grain file (8192-byte header + payload)
    data.1           -- Grain file
    ...
    data.N-1         -- Grain file (N = grain count)
```

#### **Ring buffer semantics:**

- Fixed number of grain slots (e.g., 16 grains).
- Each grain is a separate memory-mapped file.
- Writer fills grains in order (0, 1, 2, ..., N-1, 0, ...).
- Ring buffer index = `grainIndex % grainCount`.
- Oldest grain is overwritten when buffer wraps.

#### **Why separate grain files?**

- Partial grain writes (slice-by-slice for video).
- Lazy allocation (grains mapped on-demand).
- Flexible per-grain sizes (though typically uniform).
- Easier debugging (can examine individual grain files with `hexdump`).

#### **Key design:**

- **Lazy mapping**: Not all grains are mapped initially. `emplaceGrain()` maps a grain on first access. Readers only map grains they actually read.
- **Versioning**: Each grain header has a version number. Reader checks it matches `GRAIN_HEADER_VERSION`.
- **Payload offset**: Grain payload starts at offset 8192 (`MXL_GRAIN_PAYLOAD_OFFSET`). Header is fixed size with padding.

#### **Classes:**

- **`DiscreteFlowState`**: Extends `Flow` with discrete-specific fields (grain count, slice sizes, etc.).
- **`DiscreteFlowData`**: Manages the `data` file and a vector of grain mappings.

---

### `ContinuousFlowData.hpp` -- Sample Ring Buffer Management

Specialization of `FlowData` for continuous flows (AUDIO).

#### **Filesystem layout:**

```
${domain}/${flowId}.mxl-flow/
  data               -- ContinuousFlowState (ring buffer metadata, sample positions)
  channels           -- Per-channel sample ring buffers
```

#### **Memory layout of `channels` file:**

```
[Channel 0 buffer: bufferLength samples]
[Channel 1 buffer: bufferLength samples]
...
[Channel N-1 buffer: bufferLength samples]
```

#### **Ring buffer semantics:**

- Each channel has its own ring buffer of `bufferLength` samples.
- Writer appends samples, incrementing `sampleOffset`.
- Ring buffer index = `sampleOffset % bufferLength`.
- Oldest samples overwritten when buffer wraps.
- All channels synchronized (same `sampleOffset` for all).

#### **Why single `channels` file?**

- Channels always written/read together (interleaved operation).
- Single `mmap()` for all channels (efficient).
- Simpler file management than separate files per channel.
- Cache-friendly layout (channels adjacent in memory).

#### **Key design:**

- **Sample word size flexibility**: Supports different sample formats (float32, int24, etc.). Word size stored in `_sampleWordSize` (typically 4 for float32). Buffer size = `channelCount * bufferLength * sampleWordSize`.
- **Lazy channel buffer mapping**: `data` file opened in constructor. `channels` file opened via `openChannelBuffers()` call. Allows reading metadata before mapping large sample buffers.

#### **Classes:**

- **`ContinuousFlowState`**: Extends `Flow` with continuous-specific fields (channel count, buffer length, sample offset, etc.).
- **`ContinuousFlowData`**: Manages the `data` file and the `channels` file mapping.

---

## The Orchestration Layer: Factories and Managers

These classes create, open, and delete flows, coordinating between parsers, data structures, and I/O.

### `FlowManager.hpp` -- CRUD Operations for Flows

This is the **centralized authority** for flow lifecycle management. It handles all filesystem operations.

#### **Responsibilities:**

- **CREATE**: Allocate flow directory structure, shared memory files, grain files.
- **READ/OPEN**: Map existing flow files into `FlowData` objects.
- **DELETE**: Remove flow files and directories.
- **LIST**: Enumerate all flows in the domain.
- **QUERY**: Retrieve flow definitions (NMOS JSON).

#### **Filesystem structure created:**

```
${domain}/
  ${flowId}.mxl-flow/            -- Flow directory
    flow_def.json                -- NMOS flow definition (stored as-is)
    data                         -- Flow shared memory (FlowState + metadata)
    access                       -- Touch file for reader activity tracking
    grains/                      -- Grain directory (discrete flows only)
      data.0, data.1, ...        -- Individual grain files
    channels                     -- Sample ring buffer (continuous flows only)
```

#### **Design:**

- One `FlowManager` per `Instance` (owns the domain).
- No caching (FlowData objects owned by readers/writers, not manager).
- Atomic operations where possible (e.g., create with `O_EXCL`).
- Idempotent operations (e.g., delete non-existent flow succeeds).

#### **Thread safety:**

- All operations are thread-safe (use filesystem atomicity).
- No internal locking (stateless except `_mxlDomain` path).
- Concurrent creates of same flow: one succeeds, others get "already exists".

#### **Garbage collection:**

- `deleteFlow()` checks for advisory locks before deletion.
- Flows with active readers/writers cannot be deleted (lock held).
- Stale flows (no locks) can be deleted safely.

#### **Key methods:**

- `createOrOpenDiscreteFlow(...)` / `createOrOpenContinuousFlow(...)`: Create new flow or open existing. Returns `(created: bool, flowData: unique_ptr)`.
- `openFlow(flowId, mode)`: Open existing flow by UUID.
- `deleteFlow(flowId)`: Delete flow if no advisory locks held.
- `listFlows()`: Return vector of all flow UUIDs in domain.
- `getFlowDef(flowId)`: Read `flow_def.json` and return as string.

---

### `FlowParser.hpp` -- NMOS Flow Definition Parser

Parses NMOS IS-04 Flow resources (JSON) into MXL internal structures.

#### **Why NMOS?**

- Industry-standard media flow description (AMWA IS-04).
- Already supported by broadcast equipment and software.
- Rich metadata (timing, colorimetry, audio channels).
- Enables interoperability with NMOS-based systems.

#### **Example NMOS flow definition:**

```json
{
  "id": "abc-123-def-456",
  "format": "urn:x-nmos:format:video",
  "grain_rate": {"numerator": 24000, "denominator": 1001},
  "frame_width": 1920,
  "frame_height": 1080,
  "interlace_mode": "progressive",
  "components": [...]
}
```

#### **Extracted data:**

- Flow UUID (from `"id"`)
- Data format (VIDEO_V210, AUDIO_F32, etc.) derived from `"format"` and `"components"`
- Grain rate (frame rate for video, sample rate for audio)
- Dimensions, interlacing, color space (for video)
- Channel count (for audio)
- Computed payload sizes (grains, slices, samples)

#### **Design choice:**

- Parse once in constructor, cache results (parser is immutable).
- Throws on invalid JSON or missing required fields (fail-fast).
- Uses `picojson` for JSON parsing (lightweight, header-only).

#### **Key methods:**

- `getId()`: Flow UUID
- `getGrainRate()`: Frame rate or sample rate as `mxlRational`
- `getFormat()`: `mxlDataFormat` (VIDEO/AUDIO/DATA)
- `getPayloadSize()`: Computed size in bytes for one grain/sample buffer
- `getPayloadSliceLengths()`: Array of slice lengths per plane
- `getTotalPayloadSlices()`: Total slice count (for partial grain writes)
- `getChannelCount()`: Number of audio channels (audio flows only)
- `get<T>(field)`: Generic accessor for arbitrary JSON fields

---

### `FlowOptionsParser.hpp` -- Flow Creation Options Parser

Parses the optional `options` JSON string passed to `mxlCreateFlowWriter()`. Allows overriding default parameters:

- Ring buffer size (grain count, sample buffer length)
- Sync batch size hints
- Commit batch size hints
- Payload location (host vs. device memory)

Merges instance-level options with domain-wide options (instance-level takes precedence).

---

### `DomainWatcher.hpp` -- Flow Directory Monitoring

Watches the domain directory for flow changes (creation, deletion, modification). Uses `inotify` on Linux to get filesystem events. Used by garbage collection to detect stale flows.

#### **Responsibilities:**

- Monitor `${domain}` for new `.mxl-flow` directories.
- Track inode changes (flow recreation detection).
- Notify interested parties (e.g., FlowManager) of changes.

---

### `FlowIoFactory.hpp` and `PosixFlowIoFactory.hpp` -- Reader/Writer Creation

Abstract factory pattern for creating platform-specific readers and writers.

- **`FlowIoFactory`**: Abstract base class defining creation methods.
- **`PosixFlowIoFactory`**: POSIX implementation (Linux, macOS) using `mmap`, `fcntl`, `futex`.

#### **Why factory pattern?**

- Enables platform-specific implementations (future Windows, FreeBSD, etc.).
- Separates platform I/O details from flow logic.
- Simplifies testing (can mock factory for unit tests).

#### **Key methods:**

- `createDiscreteFlowReader(flowData)`: Returns `unique_ptr<DiscreteFlowReader>`.
- `createContinuousFlowReader(flowData)`: Returns `unique_ptr<ContinuousFlowReader>`.
- `createDiscreteFlowWriter(flowData)`: Returns `unique_ptr<DiscreteFlowWriter>`.
- `createContinuousFlowWriter(flowData)`: Returns `unique_ptr<ContinuousFlowWriter>`.

---

### `FlowReaderFactory.hpp` and `FlowWriterFactory.hpp` -- High-Level Creation Wrappers

Convenience wrappers around `FlowIoFactory` that handle format detection and factory dispatch. Called by `Instance::getFlowReader()` / `Instance::createFlowWriter()`.

---

## The Readers and Writers Layer: Concrete Implementations

These are the actual classes behind the opaque `mxlFlowReader` and `mxlFlowWriter` handles returned by the public API.

### `FlowReader.hpp` -- Abstract Reader Base

Abstract base class for all flow readers (VIDEO/AUDIO/DATA consumers).

#### **Responsibilities:**

- Open and validate flow (check inode for staleness).
- Provide zero-copy access to media data.
- Wait for new data using futex (efficient cross-process wait).
- Detect flow recreation (inode change).
- Return copies of metadata structures (safe to cache).

#### **Key methods:**

- `getId()`: Flow UUID
- `getDomain()`: Domain path
- `getFlowData()`: Access to underlying `FlowData` (pure virtual)
- `getFlowInfo()`: Copy of full flow metadata
- `getFlowConfigInfo()`: Copy of immutable config
- `getFlowRuntimeInfo()`: Copy of current runtime state
- `isFlowValid()`: Stale flow detection (checks inode)

#### **Usage pattern:**

1. `Instance::getFlowReader(flowId)` creates and opens reader.
2. Reader maps flow `data` file, validates inode.
3. Application calls `getGrain()` or `readSamples()` to consume media.
4. Reader waits on `syncCounter` futex if no new data available.
5. Application calls `Instance::releaseReader()` when done.

---

### `DiscreteFlowReader.hpp` -- Grain Reader Implementation

Concrete implementation of `FlowReader` for discrete flows (VIDEO/DATA).

#### **Grain API:**

- **`getGrain(index, timeout, &grain, &payload)`**: Blocks until grain at `index` is fully committed. Returns pointer to grain's payload bytes (zero-copy).
- **`getGrainSlice(index, minValidSlices, timeout, &grain, &payload)`**: Waits for at least `minValidSlices` to be committed (partial grain read).
- **`getGrainNonBlocking(index, &grain, &payload)`**: Returns immediately. If grain not ready, returns `MXL_ERR_OUT_OF_RANGE_TOO_EARLY`.
- **`getGrainSliceNonBlocking(...)`**: Non-blocking partial-grain read.

#### **Key internal details:**

- Maps grain files on demand (lazy mapping).
- Tracks ring buffer position.
- Uses `waitUntilChanged(&flowState->syncCounter, ...)` to sleep until writer signals new data.
- Detects overwritten grains (`TOO_LATE`) and not-yet-written grains (`TOO_EARLY`).

---

### `ContinuousFlowReader.hpp` -- Sample Reader Implementation

Concrete implementation of `FlowReader` for continuous flows (AUDIO).

#### **Samples API:**

- **`readSamples(index, count, timeout, &slices)`**: Reads `count` samples per channel, ending at `index` (inclusive). Returns `mxlWrappedMultiBufferSlice` describing up to two memory fragments (if range wraps around ring buffer) for all channels.
- **`readSamplesNonBlocking(index, count, &slices)`**: Non-blocking variant.

#### **Key internal details:**

- Maps `channels` file (sample ring buffer).
- Tracks per-channel read position.
- Uses `waitUntilChanged(&flowState->syncCounter, ...)` to sleep until writer signals new samples.
- Respects `bufferLength / 2` read limit (other half is write zone).

---

### `FlowWriter.hpp` -- Abstract Writer Base

Abstract base class for all flow writers (VIDEO/AUDIO/DATA producers).

#### **Responsibilities:**

- Create or open flow (create `data` file, grain files, or sample buffer).
- Provide zero-copy access for writing media.
- Update flow metadata atomically (grain counts, sample positions).
- Signal readers using futex after committing data.
- Manage advisory locks (exclusive for single writer, shared for multi-writer).
- Periodically touch files to prevent garbage collection.

#### **Exclusive vs. shared writers:**

- **Exclusive**: Only one writer, holds exclusive advisory lock (typical case).
- **Shared**: Multiple writers, each holds shared advisory lock (rare, for distributed sources).
- Writers can attempt to upgrade from shared to exclusive via `makeExclusive()`.
- Advisory locks prevent garbage collection of active flows, not data synchronization (applications must coordinate if multiple writers exist).

#### **Key methods:**

- `getId()`: Flow UUID
- `getFlowData()`: Access to underlying `FlowData` (pure virtual)
- `getFlowInfo()`: Copy of full flow metadata
- `getFlowConfigInfo()`: Copy of immutable config
- `getFlowRuntimeInfo()`: Copy of current runtime state
- `isExclusive()`: Check if exclusive lock held
- `makeExclusive()`: Attempt to upgrade from shared to exclusive (non-blocking)

---

### `DiscreteFlowWriter.hpp` -- Grain Writer Implementation

Concrete implementation of `FlowWriter` for discrete flows (VIDEO/DATA).

#### **Grain API:**

- **`getGrainInfo(index, &grain)`**: Inspect grain metadata without opening for mutation.
- **`openGrain(index, &grain, &payload)`**: Locks a grain slot for writing, returns writable pointer. Only one grain may be open at a time.
- **`commitGrain(&grain)`**: Publishes the grain. Copies `grain->flags` into shared memory, advances `headIndex` if this is the new head, wakes all waiting readers via `wakeAll(&flowState->syncCounter)`. Can call multiple times for same grain, incrementing `validSlices` each time (partial writes).
- **`cancelGrain()`**: Discards the open grain without publishing.

#### **Key internal details:**

- Allocates and manages grain files.
- Updates grain ring buffer metadata (`headIndex`, `validSlices`).
- Wakes readers via `syncCounter` futex after commit.

---

### `ContinuousFlowWriter.hpp` -- Sample Writer Implementation

Concrete implementation of `FlowWriter` for continuous flows (AUDIO).

#### **Samples API:**

- **`openSamples(index, count, &slices)`**: Opens `count` samples per channel starting at `index`. Returns `mxlMutableWrappedMultiBufferSlice` describing writable regions in shared memory.
- **`commitSamples()`**: Advances `headIndex`, wakes readers via `wakeAll(&flowState->syncCounter)`.
- **`cancelSamples()`**: Discards the open range.

#### **Key internal details:**

- Manages `channels` sample ring buffer.
- Updates sample positions and counts.
- Wakes readers via `syncCounter` futex after commit.

---

### `FlowSynchronizationGroup.hpp` -- Multi-Flow Synchronization

Allows a media function to wait for data to become available across **multiple flows** simultaneously.

#### **Typical use case:**

A video mixer that consumes one video flow and one audio flow needs to wait until both have data at the same timestamp before starting to process.

#### **Usage:**

1. `mxlCreateFlowSynchronizationGroup()`: Create empty group.
2. `mxlFlowSynchronizationGroupAddReader(group, reader)`: Add each flow reader to group.
3. `mxlFlowSynchronizationGroupWaitForDataAt(group, timestamp, timeout)`: Block until all flows have data at `timestamp`.
4. `mxlReleaseFlowSynchronizationGroup()`: Clean up when done.

#### **Internal details:**

- Converts `timestamp` to an index for each flow based on its edit rate.
- Polls each reader's `headIndex` to check availability.
- Uses `waitUntilChanged(&flowState->syncCounter, ...)` to wait for updates.
- Returns `MXL_STATUS_OK` when all flows have data, `MXL_ERR_TIMEOUT` otherwise.

---

## The Root: Instance

### `Instance.hpp` -- The Top-Level SDK Object

This is the **root object** of the SDK's internal implementation. It's the C++ class behind the opaque `mxlInstance` handle returned by the public API.

#### **Responsibilities:**

- Bind to an MXL domain (a directory on tmpfs).
- Own the `FlowManager` (CRUD operations).
- Own the `FlowIoFactory` (platform-specific I/O).
- Manage lifetime of readers, writers, and synchronization groups.
- Provide reference counting for readers/writers (multiple API calls can return the same reader).
- Garbage collect stale flows.

#### **Key classes:**

- **`Instance::RefCounted<T>`**: Internal template for reference-counted readers/writers. Each successful call to `getFlowReader()` increments the refcount. Each call to `releaseReader()` decrements it. When refcount hits zero, the reader is destroyed.

#### **Key members:**

- `_flowManager`: Performs flow CRUD operations.
- `_flowIoFactory`: Delegates creation of readers and writers.
- `_readers`: Map of flow UUIDs to `RefCounted<FlowReader>`.
- `_writers`: Map of flow UUIDs to `RefCounted<FlowWriter>`.
- `_syncGroups`: List of active synchronization groups.
- `_watcher`: `DomainWatcher` for monitoring flow changes.

#### **Key methods:**

- `createFlowWriter(flowDef, options)`: Parses JSON, creates flow via `FlowManager`, wraps in `FlowWriter`, stores in `_writers` map. Returns `(configInfo, writer, created)`.
- `getFlowReader(flowId)`: Opens flow via `FlowManager`, wraps in `FlowReader`, stores in `_readers` map (or increments refcount if already exists). Returns `FlowReader*`.
- `releaseReader(reader)` / `releaseWriter(writer)`: Decrements refcount, destroys if zero.
- `garbageCollect()`: Scans domain for flows with no advisory locks, deletes them. Returns count of deleted flows.
- `createFlowSynchronizationGroup()` / `releaseFlowSynchronizationGroup(...)`: Manage sync groups.

#### **Utility conversion functions:**

- `to_Instance(mxlInstance)`: Cast opaque C handle to C++ `Instance*`.
- `to_FlowReader(mxlFlowReader)`: Cast opaque C handle to C++ `FlowReader*`.
- `to_FlowWriter(mxlFlowWriter)`: Cast opaque C handle to C++ `FlowWriter*`.
- `to_FlowSynchronizationGroup(mxlFlowSynchronizationGroup)`: Cast opaque C handle to C++ `FlowSynchronizationGroup*`.

These functions are called by the public C API wrappers in `lib/src/` to bridge from opaque handles to concrete C++ objects.

---

## How It All Connects: The Data Flow

Let's trace the lifecycle of **writing a video grain** from end to end:

### Writer Path (Top to Bottom)

1. **Public API**: Application calls `mxlCreateFlowWriter(instance, flowDefJson, NULL, &writer, NULL, NULL)`.

2. **Instance layer**: `Instance::createFlowWriter()` is invoked.
   - Parses `flowDefJson` via `FlowParser` (extracts UUID, format, dimensions, rate).
   - Parses options via `FlowOptionsParser` (ring buffer size, hints).
   - Calls `FlowManager::createOrOpenDiscreteFlow(...)` (for VIDEO).

3. **FlowManager layer**: `FlowManager::createOrOpenDiscreteFlow()`.
   - Constructs flow directory path: `${domain}/${flowId}.mxl-flow/`.
   - Creates `flow_def.json` (writes JSON as-is).
   - Creates `data` file, maps it via `SharedMemoryInstance<Flow>` with `CREATE_READ_WRITE` and `LockMode::Exclusive`.
   - Initializes `Flow` structure: sets `mxlFlowInfo` fields (UUID, format, grain rate, ring buffer params), sets `FlowState` fields (inode, `syncCounter = 0`).
   - Creates `grains/` directory, allocates `data.0`, `data.1`, ..., `data.N-1` files.
   - Returns `(created: true, flowData: unique_ptr<DiscreteFlowData>)`.

4. **Instance layer (continued)**: Wraps `DiscreteFlowData` in `DiscreteFlowWriter` via `FlowIoFactory::createDiscreteFlowWriter()`. Stores in `_writers` map. Returns `FlowWriter*` to C API.

5. **Application**: Calls `mxlFlowWriterOpenGrain(writer, grainIndex, &grain, &payload)`.

6. **Writer layer**: `DiscreteFlowWriter::openGrain()`.
   - Computes ring buffer index: `ringIndex = grainIndex % grainCount`.
   - Maps grain file `grains/data.{ringIndex}` if not already mapped (lazy mapping via `DiscreteFlowData::emplaceGrain()`).
   - Returns `payload` pointer directly into the mapped grain file (offset 8192 bytes from start).

7. **Application**: Fills `payload` buffer with frame data (e.g., `memcpy(payload, frameData, grain.grainSize)`).

8. **Application**: Calls `mxlFlowWriterCommitGrain(writer, &grain)`.

9. **Writer layer**: `DiscreteFlowWriter::commitGrain()`.
   - Copies `grain->flags` into shared memory `grainHeader->info.flags`.
   - Copies `grain->validSlices` into shared memory `grainHeader->info.validSlices`.
   - If `grainIndex > flowState->headIndex`, updates `flowState->headIndex = grainIndex`.
   - Increments `flowState->syncCounter++`.
   - Calls `wakeAll(&flowState->syncCounter)` to wake all waiting readers.

### Reader Path (Top to Bottom)

1. **Public API**: Application calls `mxlCreateFlowReader(instance, flowId, NULL, &reader)`.

2. **Instance layer**: `Instance::getFlowReader(flowId)`.
   - Calls `FlowManager::openFlow(flowId, AccessMode::READ_ONLY)`.

3. **FlowManager layer**: `FlowManager::openFlow()`.
   - Opens `${domain}/${flowId}.mxl-flow/data` with `SharedMemoryInstance<Flow>` in `READ_ONLY` mode, `LockMode::Shared`.
   - Reads `Flow` structure to determine format (discrete vs. continuous).
   - Returns `unique_ptr<DiscreteFlowData>`.

4. **Instance layer (continued)**: Wraps in `DiscreteFlowReader` via `FlowIoFactory::createDiscreteFlowReader()`. Stores in `_readers` map. Returns `FlowReader*` to C API.

5. **Application**: Calls `mxlFlowReaderGetGrain(reader, grainIndex, timeoutNs, &grain, &payload)`.

6. **Reader layer**: `DiscreteFlowReader::getGrain()`.
   - Computes ring buffer index: `ringIndex = grainIndex % grainCount`.
   - Checks if grain is available: `flowState->headIndex >= grainIndex` and `grainHeader->info.validSlices == grainHeader->info.totalSlices`.
   - If not available, calls `waitUntilChanged(&flowState->syncCounter, oldCounter, deadline)` to sleep until writer signals.
   - When awakened, re-checks availability. If still not available after timeout, returns `MXL_ERR_TIMEOUT`.
   - If grain was overwritten (`headIndex - grainIndex >= grainCount`), returns `MXL_ERR_OUT_OF_RANGE_TOO_LATE`.
   - Maps grain file if not already mapped (lazy mapping).
   - Returns `payload` pointer directly into the mapped grain file (offset 8192 bytes from start). Zero-copy.

7. **Application**: Reads frame data directly from `payload` (e.g., passes pointer to GPU or encoder).

8. **Application**: Calls `mxlReleaseFlowReader(instance, reader)` when done.

9. **Instance layer**: `Instance::releaseReader(reader)`.
   - Decrements refcount in `_readers` map.
   - If refcount hits zero, removes entry, destroys `FlowReader`, unmaps shared memory, releases advisory lock.

---

## Why This Architecture?

**Separation of concerns**: Each layer has a single, well-defined responsibility. Primitives (SharedMemory, Sync, Time) know nothing about flows. Data structures (Flow, FlowData) know nothing about readers/writers. Managers and factories coordinate between layers.

**Platform abstraction**: All POSIX-specific details (mmap, fcntl, futex) are isolated in `SharedMemory.hpp`, `Sync.hpp`, and `PosixFlowIoFactory.hpp`. To port MXL to Windows, you'd implement `WindowsFlowIoFactory` and replace the primitives. The rest of the codebase remains unchanged.

**Zero-copy purity**: At no point does MXL copy grain payloads or sample buffers. Readers and writers get raw pointers into shared memory. The only "copies" are metadata snapshots (`mxlFlowInfo`, `mxlGrainInfo`) which are tiny (< 1 KB).

**Lazy allocation**: Grain files aren't mapped until accessed. The `channels` file for audio isn't mapped until you call `openChannelBuffers()`. Keeps memory footprint low for readers that only inspect metadata or read a subset of grains.

**Futex efficiency**: No polling loops, no busy-waiting. Readers sleep in the kernel until writers signal them. Writers only enter the kernel if there are actual waiters. Minimal CPU overhead.

**Garbage collection**: Advisory locks (not used for synchronization) allow the system to detect stale flows (no active writers) and reclaim shared memory. `Instance::garbageCollect()` scans the domain, checks for locks, deletes flows with none. Prevents resource leaks from crashed processes.

**Versioning and compatibility**: Every shared-memory structure has a version number. Readers check versions on open and reject incompatible layouts. Enables forward evolution of the SDK without breaking existing flows.

**Testability**: The factory pattern (`FlowIoFactory`) enables mocking for unit tests. You can implement a `MockFlowIoFactory` that simulates platform I/O without touching actual files. The orchestration layer and above can be tested in isolation.

---

## In Summary

This directory is the **implementation** of the promises made by the public API. It's a carefully layered architecture where each file has a single job:

- **Primitives** abstract platform details (POSIX mmap, futex, time).
- **Data structures** define shared-memory layouts (Flow, Grain, ring buffers).
- **Managers and parsers** coordinate creation, opening, deletion, and JSON parsing.
- **Readers and writers** implement the actual zero-copy I/O operations.
- **Instance** ties it all together and provides reference-counted lifetime management.

The result is a system where:

- Writers produce media directly into shared memory.
- Readers consume it without copying.
- Synchronization is efficient (futex waits, not polling).
- Garbage collection is automatic (advisory locks detect stale flows).
- The code is portable (platform details isolated).
- The design is testable (factories enable mocking).

This is the **engine room**. Applications never see it. They just call `mxlCreateFlowWriter()` and `mxlFlowReaderGetGrain()` and get zero-copy shared-memory media exchange. Behind those calls, this entire choreography unfolds.

Welcome to the machine.
