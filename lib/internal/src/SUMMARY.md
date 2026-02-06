# The Implementation: Where Theory Meets POSIX

This directory contains the **actual implementations** of the classes defined in `lib/internal/include/mxl-internal/`. This is where abstract interfaces become concrete POSIX system calls -- `mmap()`, `futex()`, `fcntl()`, `open()`, `renameat2()`, `mkdtemp()`. This is where MXL's zero-copy architecture confronts the realities of Linux kernel semantics, filesystem atomicity, and cross-process synchronization.

## The Story

The headers defined the blueprints. This directory builds the machine.

Every `SharedMemoryInstance<T>` eventually calls `mmap()`. Every `waitUntilChanged()` becomes a `syscall(SYS_futex, FUTEX_WAIT, ...)`. Every `FlowManager::createOrOpenDiscreteFlow()` becomes a series of `mkdtemp()`, `open(O_CREAT|O_EXCL)`, `posix_fallocate()`, and `renameat2(RENAME_NOREPLACE)` calls. Every reader wait on a futex translates to a kernel-level sleep on a shared-memory address.

This is where the rubber meets the road. Let's walk through the key files and see how MXL's abstractions map to POSIX reality.

---

## The Primitives Layer: POSIX System Calls Wrapped

### `SharedMemory.cpp` -- Memory-Mapped File I/O

This is the implementation of `SharedMemoryBase` and `SharedMemoryInstance<T>`, the foundation of MXL's zero-copy architecture.

#### **Constructor: Open and Map**

```cpp
SharedMemoryBase::SharedMemoryBase(const char* path, AccessMode mode, size_t payloadSize, LockMode lockMode)
```

**Step 1: Open the file**

- **Create mode** (`CREATE_READ_WRITE`): `open(path, O_EXCL | O_CREAT | O_RDWR | O_CLOEXEC, 0664)` -- creates file atomically. If it already exists, `open()` fails with `EEXIST`, and we fall through to opening it normally. The `O_EXCL` flag ensures only one process creates the file.

- **Open existing** (`READ_ONLY` or `READ_WRITE`): `open(path, (mode == READ_ONLY) ? O_RDONLY : O_RDWR)` -- opens file without creation. If it doesn't exist, `open()` fails.

**Step 2: Allocate space** (if created)

- `posix_fallocate(fd, 0, payloadSize)` -- reserves disk space (or tmpfs pages) for the file. On tmpfs this is a no-op (sparse), but on real filesystems it pre-allocates blocks. Ensures the file is large enough before we `mmap()` it.

**Step 3: Acquire advisory lock** (if requested)

- **Exclusive lock**: `flock(fd, LOCK_EX | LOCK_NB)` -- tries to acquire an exclusive lock (non-blocking). Only one process can hold this at a time. Used by single writers.

- **Shared lock**: `flock(fd, LOCK_SH | LOCK_NB)` -- tries to acquire a shared lock. Multiple processes can hold shared locks simultaneously. Used by readers or multi-writer scenarios.

- **Why advisory locks?** They're used for **garbage collection coordination**, not data synchronization. MXL checks these locks before deleting flows. If a lock is held (active reader/writer), the flow isn't deleted. If no locks are held (stale flow), it's safe to delete. We use `flock()` instead of `fcntl(F_SETLK)` because `flock` locks persist across `dup()` and are simpler to reason about.

**Step 4: Map the file into memory**

- `fstat(fd, &statBuf)` -- get the actual file size (in case it was pre-existing).

- `mmap(nullptr, statBuf.st_size, PROT_READ | (mode != READ_ONLY ? PROT_WRITE : 0), MAP_FILE | MAP_SHARED, fd, 0)` -- map the file into the process's address space. `MAP_SHARED` ensures changes are visible to other processes. `PROT_READ` alone for readers (can't accidentally corrupt data). `PROT_READ | PROT_WRITE` for writers.

**Result:** `_data` now points to a region of shared memory backed by the file. Any writes to `_data` are immediately visible to other processes that have mapped the same file. Zero copies.

#### **Destructor: Unmap and Unlock**

```cpp
~SharedMemoryBase()
{
    if (_data) munmap(_data, _mappedSize);
    if (_fd != -1) {
        flock(_fd, LOCK_UN);  // Release advisory lock
        close(_fd);
    }
}
```

Clean up in reverse order: unmap memory, release lock, close file descriptor. The kernel handles the rest (dirty pages flushed to disk/tmpfs, lock released, file descriptor freed).

#### **`touch()`** -- Update File Timestamps

```cpp
void SharedMemoryBase::touch() {
    futimens(_fd, {{0, UTIME_NOW}, {0, accessMode() == READ_ONLY ? UTIME_OMIT : UTIME_NOW}});
}
```

Updates the file's access time and (for writable mappings) modification time. Used by `FlowWriter` to signal the flow is still active, preventing premature garbage collection. The GC logic checks file mtimes to detect stale flows.

#### **`makeExclusive()`** -- Upgrade Lock

```cpp
bool SharedMemoryBase::makeExclusive() {
    if (_lockType == LockType::Exclusive) return true;  // Already exclusive
    if (flock(_fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK) return false;  // Another process holds shared lock
        throw std::system_error(errno, ...);
    }
    _lockType = LockType::Exclusive;
    return true;
}
```

Attempts to upgrade from shared to exclusive lock (non-blocking). Returns `false` if another process still holds a shared lock (can't upgrade without waiting). Used by `FlowWriter` when it needs exclusive access to update flow state.

---

### `Sync.cpp` -- Futex-Based Cross-Process Synchronization

This is the implementation of `waitUntilChanged()`, `wakeOne()`, and `wakeAll()`, MXL's efficient cross-process signaling mechanism.

#### **Linux Implementation** (`SYS_futex` syscall)

**`do_wait(void* futex, uint32_t expected, Duration timeout)`**

```cpp
syscall(SYS_futex, futex, FUTEX_WAIT, expected, &timeoutTs, nullptr, 0);
```

- **`futex`**: Address of the 32-bit counter in shared memory (e.g., `&flowState->syncCounter`).
- **`FUTEX_WAIT`**: Wait operation (put thread to sleep if value matches `expected`).
- **`expected`**: The value we expect `*futex` to have. If `*futex != expected`, the syscall returns immediately (`EAGAIN`). If `*futex == expected`, the kernel puts the thread to sleep until someone calls `FUTEX_WAKE` on this address or the timeout expires.
- **`timeoutTs`**: Relative timeout as `struct timespec`.

**Key insight:** The kernel checks `*futex == expected` **atomically** under a spinlock. This prevents the classic race where the writer signals between our load and our wait. If the value changed, we return immediately without sleeping. If it didn't change, we sleep knowing we'll be woken when it does.

**`do_wake_all(void* futex)`**

```cpp
syscall(SYS_futex, futex, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
```

- **`FUTEX_WAKE`**: Wake operation (wake threads waiting on this address).
- **`INT_MAX`**: Maximum number of threads to wake (wakes all waiters).

This syscall wakes every thread sleeping on `futex`. They all recheck the value, see that it changed, and proceed.

#### **macOS Implementation** (`os_sync_wait_on_address`)

macOS doesn't have `futex`, but it provides an equivalent API:

```cpp
os_sync_wait_on_address_with_timeout(futex, expected, sizeof(uint32_t), OS_SYNC_WAIT_ON_ADDRESS_SHARED, OS_CLOCK_MACH_ABSOLUTE_TIME, timeout);
os_sync_wake_by_address_all(futex, sizeof(uint32_t), OS_SYNC_WAKE_BY_ADDRESS_SHARED);
```

Same semantics: wait if value matches, wake all waiters. MXL abstracts this behind `do_wait()` and `do_wake_all()`, so the rest of the codebase doesn't care which OS it's on.

#### **Template Function `waitUntilChanged<T>`**

```cpp
template<typename T>
bool waitUntilChanged(T const* in_addr, T in_expected, Timepoint in_deadline) {
    auto syncObject = std::atomic_ref{*in_addr};
    while (syncObject.load(std::memory_order_acquire) == in_expected) {
        auto now = currentTime(Clock::Realtime);
        if (now >= in_deadline) return false;  // Timeout
        if (do_wait(in_addr, in_expected, in_deadline - now) == -1) {
            if (errno == EAGAIN) continue;  // Value changed, loop and recheck
            if (errno == EINTR) continue;   // Interrupted by signal, retry
            if (errno == ETIMEDOUT) return false;  // Kernel timeout
            throw std::system_error(errno, ...);
        }
    }
    return true;
}
```

**Loop structure:** Keep waiting until `*in_addr != in_expected` or deadline expires. Each iteration:

1. Atomically load current value (with acquire semantics for proper memory ordering).
2. If it changed, return `true`.
3. If deadline passed, return `false`.
4. Otherwise, call `do_wait()` to sleep until woken or timeout.
5. On wake, re-check the value (spurious wakes can happen).

**Why the loop?** Futex waits can have spurious wakes (kernel woke us even though the value didn't change). We **must** recheck the value after every wake. This is standard futex practice.

**Memory ordering:** We use `std::atomic_ref` with `memory_order_acquire` to ensure we see the writer's stores in the correct order. The writer uses `memory_order_release` when storing the counter.

---

### `Time.cpp` and `Timing.cpp` -- TAI Time and Clock Abstractions

#### **`Time.cpp`**

Implements the public C API functions from `mxl/time.h`:

- **`mxlGetCurrentIndex(editRate)`**: Reads `CLOCK_TAI` (or `CLOCK_REALTIME` if TAI unavailable), converts to nanoseconds since SMPTE ST 2059 epoch, divides by grain duration derived from `editRate`. All exact integer arithmetic.

- **`mxlTimestampToIndex(editRate, timestamp)`** and **`mxlIndexToTimestamp(editRate, index)`**: Convert between timestamps and indices using the rational edit rate. No floating point -- just multiplications and divisions of `int64_t`.

- **`mxlSleepForNs(ns)`** and **`mxlSleepUntil(timestamp)`**: Wrappers around `nanosleep()` or `clock_nanosleep(CLOCK_TAI, TIMER_ABSTIME, ...)`. Allows efficient sleeping until a specific absolute time.

- **`mxlGetTime()`**: Reads the system clock (TAI if available, otherwise REALTIME) and returns nanoseconds since epoch.

#### **`Timing.cpp`**

Internal C++ timing utilities:

- **`currentTime(Clock clock)`**: Reads `CLOCK_REALTIME` or `CLOCK_TAI` via `clock_gettime()`, returns `Timepoint`.

- **`asTimeSpec(Duration)`** and **`asTimeSpec(Timepoint)`**: Convert MXL duration/timepoint types to POSIX `struct timespec` for syscalls.

- **Arithmetic operators** for `Timepoint` and `Duration`: Enable deadline calculations like `deadline = now + timeout`.

---

### `Logging.cpp` -- Debug Output with `spdlog`

Implements the internal logging macros (`MXL_DEBUG`, `MXL_INFO`, `MXL_WARN`, `MXL_ERROR`) using the `spdlog` library.

- **Initialization:** `Instance` constructor calls `spdlog::cfg::load_env_levels("MXL_LOG_LEVEL")` to read the `MXL_LOG_LEVEL` environment variable (e.g., `export MXL_LOG_LEVEL=debug`).

- **Formatters:** Uses `fmtlib` for fast string formatting. Log messages look like:
  ```
  [2026-02-05 12:34:56.789] [info] Instance created. MXL Domain: /dev/shm/mxl
  ```

- **Thread-safe:** `spdlog` handles locking internally, so multiple threads can log concurrently.

---

### `Thread.cpp` -- Minimal Threading Wrappers

Provides lightweight POSIX pthread wrappers. Currently minimal (mostly unused in MXL). Placeholder for future platform-specific threading abstractions (Windows threads, etc.).

---

### `PathUtils.cpp` -- Filesystem Path Construction

Utility functions to build paths for MXL domain files:

- **`makeFlowDirPath(domain, flowId)`**: Returns `${domain}/${flowId}.mxl-flow/`.
- **`makeFlowDataFilePath(domain, flowId)`**: Returns `${domain}/${flowId}.mxl-flow/data`.
- **`makeGrainDirPath(domain, flowId)`**: Returns `${domain}/${flowId}.mxl-flow/grains/`.
- **`makeGrainFilePath(domain, flowId, ringIndex)`**: Returns `${domain}/${flowId}.mxl-flow/grains/data.{ringIndex}`.
- **`makeChannelFilePath(domain, flowId)`**: Returns `${domain}/${flowId}.mxl-flow/channels`.
- **`makeFlowDescriptorFilePath(flowDir)`**: Returns `${flowDir}/flow_def.json`.

Also provides UUID-to-string conversion using the `uuid.h` library.

---

### `MediaUtils.cpp` -- Media Format Calculations

Implements media-specific calculations:

- **`getV210LineLength(width)`**: Computes the byte length of one scan line in V210 format (SMPTE 425). V210 packs 10-bit Y/Cb/Cr into 32-bit words with specific padding rules. This function implements those rules.

- **`getSliceCount(height, interlaced)`**: Returns the number of slices (scan lines) in a video frame. For progressive: `height`. For interlaced: `height / 2` (each field is half the frame).

- **`getAudioBufferSize(channels, bufferLength, sampleWordSize)`**: Returns total size in bytes of the audio sample ring buffer: `channels * bufferLength * sampleWordSize`.

---

## The Data Structures Layer: Shared Memory Initialization

### `FlowData.cpp`, `FlowInfo.cpp` -- Flow Structure Lifecycle

- **`FlowData::FlowData(path, mode, lockMode)`**: Constructor calls `SharedMemoryBase` constructor to map the `data` file. Base class handles mmap and locking.

- **`FlowData::isExclusive()`** and **`FlowData::makeExclusive()`**: Forward to `SharedMemoryBase` lock management methods.

- **`FlowInfo.cpp`**: Implements stream operators (`operator<<`) for `mxlFlowInfo` and related structures, used for debug logging.

---

## The Orchestration Layer: POSIX Implementation of Flow Lifecycle

### `FlowManager.cpp` -- Atomic Flow Creation and Deletion

This is where the filesystem operations happen. FlowManager creates, opens, and deletes flows.

#### **Atomic Flow Creation Pattern**

MXL uses a **create-in-temp-then-rename** pattern to ensure atomicity:

1. **Create temporary directory**: `mkdtemp(${domain}/.mxl-tmp-XXXXXXXX)` -- creates a hidden temp dir with a unique random suffix. This won't collide with real flow dirs (which are named `${uuid}.mxl-flow`).

2. **Build flow structure in temp dir**:
   - Create `data` file via `SharedMemoryInstance<Flow>` with `CREATE_READ_WRITE`.
   - Initialize `Flow` structure: set `mxlFlowInfo` fields (UUID, format, grain rate, ring buffer params), set `FlowState` fields (inode via `fstat()`, `syncCounter = 0`).
   - Write `flow_def.json` (NMOS JSON) as-is.
   - For discrete flows: create `grains/` directory, allocate grain files `grains/data.0`, `data.1`, ..., `data.N-1`.
   - For continuous flows: create `channels` file.

3. **Atomically publish**: `renameat2(AT_FDCWD, tempDir, AT_FDCWD, ${domain}/${flowId}.mxl-flow, RENAME_NOREPLACE)` -- atomically rename the temp dir to the public name. `RENAME_NOREPLACE` ensures the rename fails with `EEXIST` if the target already exists (another process won the race).

4. **Handle race**:
   - If `renameat2()` succeeds: We created the flow. Return `(created: true, flowData)`.
   - If `renameat2()` fails with `EEXIST`: Another process created the flow first. Delete the temp dir (cleanup), then call `openFlow(flowId)` to open the winner's flow. Return `(created: false, flowData)`.

**Why this pattern?** It's a **create-or-open** semantic. Multiple processes can call `createOrOpenDiscreteFlow()` with the same UUID simultaneously. The first one to call `renameat2()` wins and creates the flow. The others seamlessly open the winner's flow. No explicit locks needed -- the filesystem provides atomicity via `RENAME_NOREPLACE`.

#### **Flow Deletion**

```cpp
bool FlowManager::deleteFlow(uuid flowId) {
    auto flowDir = makeFlowDirPath(_mxlDomain, flowId);
    std::filesystem::remove_all(flowDir);  // Recursively delete entire flow directory
    return true;
}
```

**Garbage collection** calls this after verifying the flow has no active locks (no writers). `std::filesystem::remove_all()` is a recursive directory delete (equivalent to `rm -rf`). The filesystem handles the cleanup: unmaps pages, frees inodes, reclaims disk space.

#### **Flow Opening**

```cpp
std::unique_ptr<FlowData> FlowManager::openFlow(uuid flowId, AccessMode mode) {
    auto flowDir = makeFlowDirPath(_mxlDomain, flowId);
    auto dataPath = makeFlowDataFilePath(_mxlDomain, flowId);

    // Map the data file
    auto flowSegment = SharedMemoryInstance<Flow>(dataPath.c_str(), mode, LockMode::Shared);

    // Check format (discrete vs. continuous)
    auto format = flowSegment.get()->info.config.common.format;

    if (mxlIsDiscreteDataFormat(format)) {
        return openDiscreteFlow(flowDir, std::move(flowSegment));
    } else {
        return openContinuousFlow(flowDir, std::move(flowSegment));
    }
}
```

Opens the `data` file, maps it, checks the format field, then constructs the appropriate `FlowData` subclass (`DiscreteFlowData` or `ContinuousFlowData`).

For discrete flows, `openDiscreteFlow()` doesn't map grain files yet -- that's done lazily by the reader/writer when they first access a grain.

For continuous flows, `openContinuousFlow()` doesn't map the `channels` file yet -- that's done by calling `ContinuousFlowData::openChannelBuffers()`.

---

### `FlowParser.cpp` -- NMOS JSON Parsing

Uses the `picojson` library (lightweight, header-only) to parse NMOS IS-04 Flow Resource JSON.

#### **Key parsing logic:**

- **Extract UUID**: `id = uuids::uuid::from_string(json["id"].get<string>())`.

- **Extract format**: Maps NMOS format URN (`urn:x-nmos:format:video`) to `mxlDataFormat` enum. Examines `components` array to determine specific format (e.g., V210 vs. V210a).

- **Extract grain rate**: For video/data, reads `grain_rate: {numerator, denominator}`. For audio, reads `sample_rate: {numerator, denominator}`.

- **Extract dimensions**: For video, reads `frame_width`, `frame_height`, `interlace_mode`.

- **Extract channel count**: For audio, reads channel count from flow definition.

- **Compute payload size**: Based on format and dimensions. For V210 1920x1080: `height * getV210LineLength(width)`. For audio: `channels * samples_per_period * sizeof(float32)`.

- **Compute slice lengths**: For video, computes bytes per scan line per plane. V210: `sliceSizes[0] = getV210LineLength(width)`, rest are 0.

#### **Error handling:**

If required fields are missing or values are out of range, `FlowParser` throws `std::runtime_error` or `std::invalid_argument`. This is a **fail-fast** design -- better to catch invalid JSON at flow creation time than to silently create a malformed flow.

---

### `FlowOptionsParser.cpp` -- Options JSON Parsing

Parses the optional `options` string passed to `mxlCreateFlowWriter()`. Extracts:

- **Ring buffer size hints** (grain count, sample buffer length).
- **Batch size hints** (`maxCommitBatchSizeHint`, `maxSyncBatchSizeHint`).
- **History duration** (how much media history to keep in the ring buffer).

Merges with domain-wide options (read from `${domain}/options.json` if it exists). Instance-level options override domain-wide options.

---

### `DomainWatcher.cpp` -- Filesystem Monitoring with `inotify`

Uses Linux `inotify` to watch the domain directory for changes:

- `inotify_add_watch(fd, domain, IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM)` -- registers interest in creation/deletion/renaming of files in the domain.

- Background thread reads inotify events from the file descriptor and processes them.

- Used by garbage collection to detect when flows are created or deleted.

- On macOS, would use `kqueue` with `EVFILT_VNODE` instead (future work).

---

### `Instance.cpp` -- Top-Level Orchestration

This is the main orchestration layer. It ties everything together.

#### **Constructor: Initialize Domain**

```cpp
Instance::Instance(filesystem::path const& mxlDomain, string const& options, unique_ptr<FlowIoFactory>&& flowIoFactory, DomainWatcher::ptr watcher)
    : _flowManager{mxlDomain}
    , _flowIoFactory{std::move(flowIoFactory)}
    , _historyDuration{200'000'000ULL}  // Default: 200ms
{
    initializeLogging();  // Setup spdlog, read MXL_LOG_LEVEL env var
    parseOptions(options);  // Parse instance-level options
    MXL_DEBUG("Instance created. MXL Domain: {}", mxlDomain.string());
}
```

- Initializes `FlowManager` (binds to domain).
- Stores `FlowIoFactory` (for creating platform-specific readers/writers).
- Reads history duration from options or uses default (200ms).
- Sets up logging.

#### **Creating a Writer**

```cpp
tuple<mxlFlowConfigInfo, FlowWriter*, bool> Instance::createFlowWriter(string const& flowDef, optional<string> options) {
    // 1. Parse NMOS JSON
    FlowParser parser{flowDef};
    FlowOptionsParser optsParser{options.value_or("")};

    // 2. Calculate ring buffer size from history duration + grain/sample rate
    auto grainRate = parser.getGrainRate();
    auto grainCount = calculateGrainCount(_historyDuration, grainRate);  // For discrete
    auto bufferLength = calculateBufferLength(_historyDuration, grainRate);  // For continuous

    // 3. Create or open flow via FlowManager
    auto [created, flowData] = mxlIsDiscreteDataFormat(parser.getFormat())
        ? _flowManager.createOrOpenDiscreteFlow(parser.getId(), flowDef, ...)
        : _flowManager.createOrOpenContinuousFlow(parser.getId(), flowDef, ...);

    // 4. Wrap FlowData in platform-specific writer
    auto writer = _flowIoFactory->createDiscreteFlowWriter(std::move(flowData));  // Or createContinuousFlowWriter

    // 5. Store in _writers map with refcount=1
    auto [it, inserted] = _writers.emplace(parser.getId(), RefCounted<FlowWriter>{std::move(writer)});

    // 6. Return config, writer pointer, and created flag
    return {flowData->flowInfo()->config, it->second.get(), created};
}
```

**Key steps:** Parse JSON, compute buffer sizes from history duration, delegate creation to `FlowManager`, wrap in platform-specific writer via factory, store in map with refcount, return to caller.

#### **Getting a Reader**

```cpp
FlowReader* Instance::getFlowReader(string const& flowId) {
    lock_guard lock{_mutex};

    auto uuid = uuids::uuid::from_string(flowId);

    // Check if reader already exists (refcounted)
    auto it = _readers.find(uuid);
    if (it != _readers.end()) {
        it->second.addReference();  // Increment refcount
        return it->second.get();
    }

    // Open flow via FlowManager
    auto flowData = _flowManager.openFlow(uuid, AccessMode::READ_ONLY);

    // Wrap in platform-specific reader via factory
    auto reader = mxlIsDiscreteDataFormat(flowData->flowInfo()->config.common.format)
        ? _flowIoFactory->createDiscreteFlowReader(std::move(flowData))
        : _flowIoFactory->createContinuousFlowReader(std::move(flowData));

    // Store in _readers map with refcount=1
    auto [newIt, inserted] = _readers.emplace(uuid, RefCounted<FlowReader>{std::move(reader)});

    return newIt->second.get();
}
```

**Refcounting:** If the reader already exists, just increment refcount and return existing pointer. Otherwise, create new reader, store in map with refcount=1. This allows multiple API calls to return the same reader instance (saves memory, avoids duplicate mappings).

#### **Releasing a Reader**

```cpp
void Instance::releaseReader(FlowReader* reader) {
    lock_guard lock{_mutex};

    auto uuid = reader->getId();
    auto it = _readers.find(uuid);
    if (it == _readers.end()) return;  // Already released

    if (it->second.releaseReference()) {  // Decrement refcount, returns true if now 0
        _readers.erase(it);  // Destroy reader, unmap shared memory, release advisory lock
    }
}
```

Decrements refcount. If refcount hits 0, erases from map, which destroys the `RefCounted<FlowReader>`, which calls `~FlowReader()`, which unmaps shared memory and releases advisory locks.

#### **Garbage Collection**

```cpp
size_t Instance::garbageCollect() const {
    size_t deletedCount = 0;

    // List all flows in domain
    auto flowIds = _flowManager.listFlows();

    for (auto const& flowId : flowIds) {
        auto dataPath = makeFlowDataFilePath(_flowManager.getDomain(), flowId);

        // Try to get exclusive lock (non-blocking)
        int fd = open(dataPath.c_str(), O_RDONLY);
        if (fd == -1) continue;

        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            // Got exclusive lock -> no writers -> stale flow
            flock(fd, LOCK_UN);
            close(fd);
            _flowManager.deleteFlow(flowId);
            deletedCount++;
        } else {
            // Couldn't get lock -> active writers -> skip
            close(fd);
        }
    }

    return deletedCount;
}
```

Scans domain for all flows, tries to get exclusive lock on each `data` file. If successful (no writers), deletes the flow. Returns count of deleted flows. This is a **best-effort** operation -- never throws, just logs errors.

#### **Destructor: Clean Up Leaked Writers**

```cpp
Instance::~Instance() {
    for (auto& [id, writer] : _writers) {
        if (writer.get()->isExclusive() || writer.get()->makeExclusive()) {
            // Writer has exclusive lock -> we created this flow -> delete it
            MXL_WARN("Cleaning up flow '{}' of leaked flow writer.", uuids::to_string(id));
            _flowManager.deleteFlow(id);
        }
    }
}
```

If application forgets to call `releaseWriter()`, the `Instance` destructor cleans up. For exclusive writers (typical case), it deletes the flow. For shared writers, it can't tell if other processes are using the flow, so it leaves it alone.

---

## The Readers and Writers Layer: POSIX Implementations

### `PosixDiscreteFlowReader.cpp` -- Grain Reader

Concrete implementation of `DiscreteFlowReader` for POSIX systems.

#### **`getGrain(index, timeout, &grain, &payload)`**

1. **Validate index**: Check if grain is too late (overwritten by ring buffer wrap) or too early (not yet written).

```cpp
auto headIndex = flowState()->headIndex;
if (grainIndex < headIndex - grainCount) {
    return MXL_ERR_OUT_OF_RANGE_TOO_LATE;  // Overwritten
}
if (grainIndex > headIndex) {
    // Not yet written, wait for it
}
```

2. **Wait loop**: If grain not available, wait on `syncCounter` futex.

```cpp
auto deadline = currentTime(Clock::Realtime) + Duration{timeout};
while (flowState()->headIndex < grainIndex) {
    auto oldCounter = flowState()->syncCounter;
    if (!waitUntilChanged(&flowState()->syncCounter, oldCounter, deadline)) {
        return MXL_ERR_TIMEOUT;
    }
}
```

3. **Map grain file** (if not already mapped): Lazy mapping via `DiscreteFlowData::emplaceGrain()`.

```cpp
auto ringIndex = grainIndex % grainCount;
auto grainSegment = _flowData->getGrain(ringIndex);  // Returns SharedMemoryInstance<Grain>
if (!grainSegment) {
    // Not mapped yet, map it now
    auto grainPath = makeGrainFilePath(_domain, _flowId, ringIndex);
    grainSegment = SharedMemoryInstance<Grain>(grainPath.c_str(), AccessMode::READ_ONLY, LockMode::None);
    _flowData->emplaceGrain(ringIndex, std::move(grainSegment));
}
```

4. **Check validity**: Check `grainHeader->info.validSlices == totalSlices` (full grain available).

5. **Return pointer**: `*payload = grainSegment->get()->header.pad + sizeof(mxlGrainInfo)` (points to start of payload, offset 8192 bytes from file start). Zero-copy.

---

### `PosixDiscreteFlowWriter.cpp` -- Grain Writer

Concrete implementation of `DiscreteFlowWriter` for POSIX systems.

#### **`openGrain(index, &grain, &payload)`**

1. **Compute ring buffer index**: `ringIndex = grainIndex % grainCount`.

2. **Map grain file** (if not already mapped): Same lazy mapping as reader.

3. **Return writable pointer**: `*payload = grainSegment->get()->header.pad + sizeof(mxlGrainInfo)` (writable because mapping is `PROT_READ | PROT_WRITE`).

4. **Store open grain index**: `_openGrainIndex = grainIndex` (enforce single-open-grain-at-a-time constraint).

#### **`commitGrain(&grain)`**

1. **Copy flags and slice count into shared memory**:

```cpp
auto ringIndex = grain->index % grainCount;
auto grainSegment = _flowData->getGrain(ringIndex);
grainSegment->get()->header.info.flags = grain->flags;
grainSegment->get()->header.info.validSlices = grain->validSlices;
```

2. **Update head index** (if this is the new head):

```cpp
if (grain->index > flowState()->headIndex) {
    flowState()->headIndex = grain->index;
}
```

3. **Increment sync counter and wake readers**:

```cpp
flowState()->syncCounter++;
wakeAll(&flowState()->syncCounter);
```

4. **Clear open grain index**: `_openGrainIndex = MXL_UNDEFINED_INDEX` (allow next grain to be opened).

---

### `PosixContinuousFlowReader.cpp` and `PosixContinuousFlowWriter.cpp` -- Sample Reader/Writer

Similar structure to discrete reader/writer, but:

- **Reader**: Computes byte offsets into the `channels` ring buffer, returns `mxlWrappedMultiBufferSlice` describing up to two fragments (if range wraps around).

- **Writer**: Same fragment logic, but writable pointers. Increments `headIndex` by number of samples committed.

Both use `waitUntilChanged(&flowState()->syncCounter, ...)` and `wakeAll(&flowState()->syncCounter)` for synchronization.

---

### `FlowSynchronizationGroup.cpp` -- Multi-Flow Sync

Implements the synchronization group API.

#### **`addReader(reader)`**

Adds a reader to the internal list. Stores the reader pointer, its edit rate, and the minimum slice count (for discrete readers).

#### **`waitForDataAt(timestamp, timeout)`**

```cpp
auto deadline = currentTime(Clock::Realtime) + Duration{timeout};

for (auto const& entry : _readers) {
    // Convert timestamp to index for this flow's edit rate
    auto index = mxlTimestampToIndex(&entry.editRate, timestamp);

    // Wait until this flow has data at this index
    while (entry.reader->getFlowRuntimeInfo().headIndex < index) {
        auto oldCounter = entry.reader->getFlowData().flowState()->syncCounter;
        if (!waitUntilChanged(&entry.reader->getFlowData().flowState()->syncCounter, oldCounter, deadline)) {
            return MXL_ERR_TIMEOUT;
        }
    }
}

return MXL_STATUS_OK;  // All flows have data at timestamp
```

Polls each reader's `headIndex` futex in turn until all flows have data at the requested timestamp. This is a **sequential check** (not parallel), but each wait is efficient (futex sleep, not polling).

---

### `PosixFlowIoFactory.cpp` -- Platform-Specific Factory

Implements the `FlowIoFactory` interface for POSIX systems.

```cpp
unique_ptr<DiscreteFlowReader> PosixFlowIoFactory::createDiscreteFlowReader(unique_ptr<DiscreteFlowData>&& flowData) {
    return make_unique<PosixDiscreteFlowReader>(std::move(flowData));
}

unique_ptr<DiscreteFlowWriter> PosixFlowIoFactory::createDiscreteFlowWriter(unique_ptr<DiscreteFlowData>&& flowData) {
    return make_unique<PosixDiscreteFlowWriter>(std::move(flowData));
}

// Similar for continuous readers/writers
```

Simple factory that constructs the POSIX-specific reader/writer classes. To port MXL to Windows, you'd implement `WindowsFlowIoFactory` and `WindowsDiscreteFlowReader`, etc.

---

## The Complete Flow: From API Call to POSIX Syscall

Let's trace a complete **write-and-read cycle** from top to bottom, all the way down to kernel interactions:

### Writer Side

1. **Application**: `mxlCreateFlowWriter(instance, flowDefJson, NULL, &writer, NULL, NULL)`

2. **Public API wrapper** (`lib/src/mxl.cpp`): Calls `Instance::createFlowWriter(flowDefJson)`.

3. **Instance**: Parses JSON, computes buffer sizes, calls `FlowManager::createOrOpenDiscreteFlow()`.

4. **FlowManager**: Creates temp dir via `mkdtemp()`, opens `data` file via `open(O_CREAT|O_EXCL)`, mmaps via `SharedMemoryInstance<Flow>` constructor (calls `mmap()`), initializes `Flow` structure, creates grain files, renames temp dir via `renameat2(RENAME_NOREPLACE)`. Returns `DiscreteFlowData`.

5. **Instance**: Wraps `DiscreteFlowData` in `PosixDiscreteFlowWriter`, stores in `_writers` map, returns `FlowWriter*`.

6. **Application**: `mxlFlowWriterOpenGrain(writer, grainIndex, &grain, &payload)`

7. **PosixDiscreteFlowWriter**: Computes `ringIndex = grainIndex % grainCount`, lazy-maps grain file via `mmap()`, returns `payload` pointer directly into shared memory.

8. **Application**: Fills `payload` buffer (e.g., `memcpy(payload, frameData, grainSize)`).

9. **Application**: `mxlFlowWriterCommitGrain(writer, &grain)`

10. **PosixDiscreteFlowWriter**:
    - Copies `grain->flags` and `grain->validSlices` into shared memory.
    - Updates `flowState()->headIndex`.
    - Increments `flowState()->syncCounter`.
    - Calls `wakeAll(&flowState()->syncCounter)`, which calls `syscall(SYS_futex, FUTEX_WAKE, INT_MAX)`.

11. **Kernel**: Wakes all threads sleeping on that futex address (readers waiting for new grain).

### Reader Side

1. **Application**: `mxlCreateFlowReader(instance, flowId, NULL, &reader)`

2. **Public API wrapper**: Calls `Instance::getFlowReader(flowId)`.

3. **Instance**: Calls `FlowManager::openFlow(flowId)`, which opens `data` file via `open(O_RDONLY)`, mmaps via `SharedMemoryInstance<Flow>` constructor. Returns `DiscreteFlowData`.

4. **Instance**: Wraps in `PosixDiscreteFlowReader`, stores in `_readers` map, returns `FlowReader*`.

5. **Application**: `mxlFlowReaderGetGrain(reader, grainIndex, timeoutNs, &grain, &payload)`

6. **PosixDiscreteFlowReader**:
    - Checks if `flowState()->headIndex >= grainIndex`. If not:
    - Enters wait loop: `while (flowState()->headIndex < grainIndex)`:
      - Loads `oldCounter = flowState()->syncCounter` (atomic load).
      - Calls `waitUntilChanged(&flowState()->syncCounter, oldCounter, deadline)`.
      - This calls `syscall(SYS_futex, FUTEX_WAIT, oldCounter, ...)`.

7. **Kernel**: Atomically checks if `flowState()->syncCounter == oldCounter`. If yes, puts thread to sleep on a wait queue associated with that memory address. Thread sleeps until writer calls `FUTEX_WAKE`.

8. **Writer commits grain**: Kernel wakes reader thread.

9. **Reader**: Returns from `waitUntilChanged()`, re-checks `flowState()->headIndex >= grainIndex`. Now true.

10. **Reader**: Lazy-maps grain file via `mmap()`, returns `payload` pointer directly into shared memory.

11. **Application**: Reads frame data directly from `payload` (zero-copy).

---

## Why This Implementation?

**Platform abstraction achieved:** All POSIX-specific details (mmap, futex, fcntl, mkdtemp, renameat2) are isolated in this `src/` directory. To port to Windows, you'd implement `WindowsSharedMemory`, `WindowsSync`, `WindowsFlowManager`, etc., using Windows APIs (`CreateFileMapping`, `MapViewOfFile`, `WaitOnAddress`, `CreateDirectory`, `MoveFileEx`). The headers in `include/mxl-internal/` would remain unchanged. The public API would remain unchanged.

**Atomicity via filesystem:** The `mkdtemp` + `renameat2(RENAME_NOREPLACE)` pattern provides **lock-free** create-or-open semantics. No need for distributed locks or coordination. The filesystem handles it.

**Efficiency via futex:** Zero CPU overhead for readers waiting on writers. No polling loops. Threads sleep in the kernel until data is ready. Wakes are batched (wake all readers at once). Writers only enter the kernel if there are actual waiters.

**Safety via advisory locks:** Garbage collection uses advisory locks to detect stale flows. Active readers/writers hold locks, preventing premature deletion. Crashed processes release locks automatically (kernel cleans them up). No resource leaks.

**Lazy allocation:** Grain files and sample buffers are only mapped when first accessed. Keeps memory footprint low. Readers that only inspect metadata don't pay the cost of mapping payloads.

**Fail-fast parsing:** `FlowParser` throws on invalid JSON. Better to catch errors at flow creation time than to silently create a malformed flow.

**Refcounted lifetime:** `Instance` manages reader/writer lifetime with reference counting. Multiple API calls can return the same reader (no duplicate mappings). Cleanup is automatic when refcount hits zero.

**Logging for observability:** Every major operation (flow creation, reader/writer lifecycle, synchronization events) is logged via `spdlog`. Controlled by `MXL_LOG_LEVEL` env var. Invaluable for debugging.

---

## In Summary

This directory is where **MXL's abstractions meet reality**. Every promise made by the public API is fulfilled here through careful use of POSIX system calls:

- **Zero-copy?** `mmap()` gives us shared memory pages.
- **Efficient waits?** `futex` gives us kernel-level sleep/wake.
- **Atomic creation?** `mkdtemp` + `renameat2(RENAME_NOREPLACE)` gives us filesystem atomicity.
- **Garbage collection?** `flock()` advisory locks give us stale-flow detection.
- **Timing precision?** `clock_gettime(CLOCK_TAI)` and exact rational arithmetic give us frame-accurate timestamps.
- **Portability?** All platform details isolated here, behind clean C++ interfaces.

The result: A zero-copy, futex-synchronized, garbage-collected, ring-buffer-based shared-memory media exchange system that scales from single-machine pipelines to multi-process broadcast workflows. Built on standard POSIX primitives, wrapped in clean abstractions, exposed via a portable C API.

This is the machine. It works.
