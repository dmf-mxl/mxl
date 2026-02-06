# The Public C API: Your Front Door to MXL

This directory is the **front door** to the Media eXchange Layer SDK. Everything a client application needs to send or receive video, audio, and data through zero-copy shared memory lives here, exposed as a clean, portable C API.

## The Story

Picture this: you're building a media processing pipeline. You need to move 4K video frames between a capture card, a GPU compositor, and an encoder. Traditional approaches force you to copy megabytes of pixels across process boundaries, burning CPU cycles and wrecking your latency budget.

MXL solves this with **ring buffers in shared memory**. Producers write frames directly into tmpfs-backed files; consumers read them with zero copies. But to make this work reliably across heterogeneous processes, you need a contract -- data structures, timing models, error codes, and lifecycle management that everyone understands.

That contract is defined in these seven headers.

---

## The Cast of Characters

### `platform.h` -- The Foundation

Every other header includes this file first. It's the **portability layer** that papers over differences between compilers and between C and C++ modes.

- **`MXL_EXPORT`** marks functions for visibility in the shared library (uses `__attribute__((visibility("default")))` on GCC/Clang, expands to nothing on other compilers).
- **`MXL_NODISCARD`** emits `[[nodiscard]]` in C++ mode so you get compiler warnings if you ignore return values. Falls back to nothing in plain C.
- **`MXL_CONSTEXPR`** maps to `constexpr` in C++, `inline` in C -- lets the same header work in both languages.

This is the invisible scaffolding that makes the rest of the API "just work" whether you're calling it from C, C++, GCC, Clang, or (in theory) MSVC.

---

### `rational.h` -- Precision Over Convenience

Media timing demands **exact arithmetic**. Floating-point math introduces rounding errors that accumulate over thousands of frames, drifting you out of sync. MXL avoids this by representing every rate as a rational number.

- **`mxlRational`**: A simple struct with `numerator` and `denominator` (both `int64_t`).
  - 50 fps? `{50, 1}`
  - 29.97 fps NTSC? `{30000, 1001}`
  - 48 kHz audio? `{48000, 1}`

This structure appears everywhere: frame rates, sample rates, edit rates. All index-to-timestamp conversions use integer arithmetic on these rationals, guaranteeing bit-exact reproducibility across machines.

- **`MXL_UNDEFINED_INDEX`**: A sentinel value (`UINT64_MAX`) returned by timing functions when the edit rate is invalid or missing. Always check for this before using an index.

---

### `dataformat.h` -- Two Worlds, One API

Media comes in two fundamentally different forms, and MXL's I/O model adapts to match:

#### **Discrete Flows** (VIDEO, DATA)
Media is delivered as **grains** -- self-contained chunks like a video frame or a block of ancillary data. Each grain occupies a slot in the ring buffer. You read/write one grain at a time via the **Grain API**.

#### **Continuous Flows** (AUDIO)
Media is a never-ending stream of **samples**, organized in per-channel ring buffers. You read/write arbitrary windows via the **Samples API**.

The helpers here let you query which model a flow uses:

- **`mxlDataFormat`**: An enum with `VIDEO`, `AUDIO`, `DATA`, `UNSPECIFIED`. Maps 1:1 to the AMWA NMOS IS-04 format URNs.
- **`mxlIsValidDataFormat()`**: Is this a recognized format?
- **`mxlIsSupportedDataFormat()`**: Is this format compiled into this build? (Same as "valid" today, but future builds might disable audio or video selectively.)
- **`mxlIsDiscreteDataFormat()`**: True for VIDEO and DATA.
- **`mxlIsContinuousDataFormat()`**: True for AUDIO.

Before you call a grain function or a samples function, check the format. Calling the wrong API on the wrong flow type returns an error.

---

### `mxl.h` -- The Root Object and Error Model

This is **mission control**. It defines the SDK's versioning, error codes, and the root object that owns everything else.

#### **`mxlStatus`** -- Universal Return Codes
Every function in MXL returns an `mxlStatus`. The contract is simple:
- `MXL_STATUS_OK` = success, your [out] parameters are valid.
- Anything else = failure, your [out] parameters are garbage.

The codes tell a story:
- **`MXL_ERR_FLOW_NOT_FOUND`**: The UUID you asked for doesn't exist in the domain.
- **`MXL_ERR_OUT_OF_RANGE_TOO_LATE`**: The writer wrapped around and overwrote that grain before you read it. You're too slow.
- **`MXL_ERR_OUT_OF_RANGE_TOO_EARLY`**: The writer hasn't produced that grain yet. You're too fast (or the writer stalled).
- **`MXL_ERR_TIMEOUT`**: You waited, but the data didn't show up in time.
- **`MXL_ERR_FLOW_INVALID`**: The flow's backing file changed (writer restarted). You must recreate your reader.

Additional codes starting at 1024 are reserved for the **Fabrics** networking layer (remote MXL over RDMA, not covered in this directory).

#### **`mxlVersionType`** and **`mxlGetVersion()`**
Query the SDK's semantic version at runtime. The `full` string is owned by the library -- don't free it.

#### **`mxlInstance`** -- The Root Object
An opaque handle representing a connection to one **MXL domain** -- a directory on tmpfs (typically `/dev/shm/mxl`) where all flow files live.

- **`mxlCreateInstance(domainPath, options)`**: Binds to a domain. Automatically calls garbage collection to clean up stale flows from crashed processes.
- **`mxlGarbageCollectFlows(instance)`**: Scans the domain for flows that no longer have a writer (detected via advisory file locks) and deletes them. Call this periodically in long-running services.
- **`mxlDestroyInstance(instance)`**: Tears down the instance and **releases all readers and writers** you created through it. Don't use the handle after this.

One process can create multiple instances pointing to different domains, but typically you create one instance per domain and pass it around.

---

### `flowinfo.h` -- The Shared Memory Contract

This is the **binary format specification** for the metadata that lives in every flow's shared memory.

Every flow stores a 2048-byte header in `${domain}/${flowId}.mxl-flow/data`. Writers mmap it read-write; readers mmap it read-only. This header is the **source of truth** for everything about the flow: what media it carries, how big the ring buffer is, where the write cursor is, etc.

#### **Structure Hierarchy**

1. **`mxlFlowInfo`** (2048 bytes total) -- The top-level container:
   - `version` (currently 1)
   - `size` (always 2048 for sanity checks)
   - `config` (immutable after creation)
   - `runtime` (updated by the writer as data is produced)
   - `reserved[1784]` (future expansion)

2. **`mxlFlowConfigInfo`** -- The immutable configuration:
   - **`mxlCommonFlowConfigInfo`** (128 bytes) -- format-independent metadata:
     - `id[16]`: 128-bit UUID (raw bytes)
     - `format`: `mxlDataFormat` (VIDEO/AUDIO/DATA)
     - `grainRate`: `mxlRational` (frame rate for video, sample rate for audio)
     - `maxCommitBatchSizeHint`: max items per commit (samples for audio, slices for video)
     - `maxSyncBatchSizeHint`: how often to wake readers (larger = fewer wakes, higher latency)
     - `payloadLocation`: `HOST_MEMORY` (tmpfs) or `DEVICE_MEMORY` (GPU)
     - `deviceIndex`: which GPU if payload is on device
   - **Union of format-specific config**:
     - **`mxlDiscreteFlowConfigInfo`** (for VIDEO/DATA):
       - `sliceSizes[4]`: bytes per slice in each payload plane (a "slice" is a scan line for video, a byte for data)
       - `grainCount`: number of grain slots in the ring buffer
     - **`mxlContinuousFlowConfigInfo`** (for AUDIO):
       - `channelCount`: number of independent audio channels
       - `bufferLength`: number of sample slots per channel (readers can access up to `bufferLength / 2` at once)

3. **`mxlFlowRuntimeInfo`** (64 bytes) -- Mutable state updated by the writer:
   - `headIndex`: the most recently committed grain/sample index (the write cursor)
   - `lastWriteTime`: TAI timestamp of the last commit
   - `lastReadTime`: TAI timestamp of the last read (updated by readers touching the `access` file)

4. **`mxlGrainInfo`** (4096 bytes) -- Per-grain metadata stored at the start of each grain file:
   - `index`: absolute grain index since epoch
   - `flags`: bitfield (e.g., `MXL_GRAIN_FLAG_INVALID` = grain doesn't contain valid media)
   - `grainSize`: total payload size in bytes
   - `totalSlices`: how many slices make a complete grain
   - `validSlices`: how many slices have been committed so far (enables partial/progressive writes)

#### **The Payload Location Story**

Most flows use `HOST_MEMORY`: payloads live in tmpfs, accessible via `mmap()`. But for GPU-accelerated workflows (e.g., an encoder that reads textures directly from CUDA device memory), you set `payloadLocation = DEVICE_MEMORY` and specify `deviceIndex`. In that case, the grain/channel files contain device pointers or handles, not host-accessible bytes.

#### **Partial Writes and Low-Latency Reads**

Discrete flows support **partial grain commits**. A video writer can commit scan lines incrementally, updating `validSlices` each time. Readers waiting on `minValidSlices` wake up as soon as enough slices are ready, enabling sub-frame-latency processing (e.g., a line-based compositor that starts work before the full frame arrives).

---

### `flow.h` -- The Main Event

This is the **heart of the API**. If you're reading or writing media, you're using functions from this header.

#### **Lifecycle: Writers and Readers**

- **`mxlCreateFlowWriter(instance, flowDefJson, options, &writer, &configInfo, &created)`**:
  - Parses the NMOS IS-04 Flow Resource JSON.
  - If the flow UUID doesn't exist, allocates shared memory files (ring buffers, grain slots, channel buffers) and writes the JSON to `flow_def.json`.
  - If it already exists, opens it for writing.
  - Returns a `mxlFlowWriter` handle.

- **`mxlReleaseFlowWriter(instance, writer)`**: Closes the writer, releases the advisory lock on `data`, making the flow eligible for garbage collection if no other writers exist.

- **`mxlCreateFlowReader(instance, flowId, options, &reader)`**: Opens an existing flow in read-only mode, mmaps the `data` file and grain/channel files.

- **`mxlReleaseFlowReader(instance, reader)`**: Unmaps shared memory, frees the reader.

#### **Introspection**

- **`mxlIsFlowActive(instance, flowId, &isActive)`**: Checks if anyone holds a write lock on the flow (non-blocking). Useful for monitoring.
- **`mxlGetFlowDef(instance, flowId, buffer, &bufferSize)`**: Reads the JSON flow definition from disk. If the buffer is too small, returns the required size so you can retry.

#### **Reading Info from a Reader**

Once you have a `mxlFlowReader`, you can snapshot its metadata:
- **`mxlFlowReaderGetInfo(reader, &info)`**: Full 2048-byte header (config + runtime).
- **`mxlFlowReaderGetConfigInfo(reader, &configInfo)`**: Immutable config only (cacheable).
- **`mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo)`**: Current head index and timestamps (changes as the writer produces data).

#### **Discrete Grain API (VIDEO / DATA)**

Readers wait for grains, get a read-only pointer into shared memory:

- **`mxlFlowReaderGetGrain(reader, index, timeoutNs, &grain, &payload)`**: Blocks until the grain at `index` is fully committed (`validSlices == totalSlices`). Returns a pointer to the grain's payload bytes. Zero copies -- you're looking directly at shared memory.

- **`mxlFlowReaderGetGrainSlice(reader, index, minValidSlices, timeoutNs, &grain, &payload)`**: Waits for at least `minValidSlices` to be committed. Use `MXL_GRAIN_VALID_SLICES_ANY` (0) to accept any, `MXL_GRAIN_VALID_SLICES_ALL` to wait for completion.

- **`mxlFlowReaderGetGrainNonBlocking(reader, index, &grain, &payload)`**: Returns immediately. If the grain isn't ready, returns `MXL_ERR_OUT_OF_RANGE_TOO_EARLY`.

- **`mxlFlowReaderGetGrainSliceNonBlocking(...)`**: Non-blocking partial-grain read.

Writers produce grains in a two-phase commit:

- **`mxlFlowWriterOpenGrain(writer, index, &grain, &payload)`**: Locks a grain slot for writing, returns a writable pointer. Only one grain may be open at a time.

- **`mxlFlowWriterCommitGrain(writer, &grain)`**: Publishes the grain. Copies `grain->flags` into shared memory, advances `headIndex` if this is the new head, and wakes all waiting readers via futex. You can call this multiple times for the same grain, incrementing `validSlices` each time, to support partial writes.

- **`mxlFlowWriterCancelGrain(writer)`**: Discards the open grain without publishing.

- **`mxlFlowWriterGetGrainInfo(writer, index, &grain)`**: Inspect a grain's metadata without opening it for mutation.

#### **Continuous Sample API (AUDIO)**

Readers access windows of samples:

- **`mxlFlowReaderGetSamples(reader, index, count, timeoutNs, &slices)`**: Reads `count` samples per channel, **ending at** `index` (inclusive). Returns a `mxlWrappedMultiBufferSlice` describing up to two memory fragments (if the range wraps around the ring buffer) for all channels. The geometry is:
  - `base.fragments[0/1]`: contiguous byte ranges for channel 0.
  - `stride`: byte offset to add for each subsequent channel.
  - `count`: total number of channels.

- **`mxlFlowReaderGetSamplesNonBlocking(...)`**: Non-blocking variant.

Writers produce samples in a similar open/commit pattern:

- **`mxlFlowWriterOpenSamples(writer, index, count, &slices)`**: Opens `count` samples per channel starting at `index`. Returns writable pointers.

- **`mxlFlowWriterCommitSamples(writer)`**: Advances the head index, wakes readers.

- **`mxlFlowWriterCancelSamples(writer)`**: Discards the open range.

#### **Buffer Slices: Handling the Wrap**

Ring buffers wrap around. A logical byte range might map to **two** physical fragments:

- **`mxlBufferSlice`**: One contiguous read-only fragment (`pointer`, `size`).
- **`mxlMutableBufferSlice`**: One writable fragment.
- **`mxlWrappedBufferSlice`**: Up to two fragments covering a range that may wrap.
- **`mxlWrappedMultiBufferSlice`**: A wrapped slice replicated across N channels with a fixed `stride`.

These types show up in the samples API and in any code that needs to iterate over ring-buffer regions.

#### **Synchronization Groups**

Real media functions often consume multiple flows (video + audio) and need to wait until **all** have data at a given timestamp before proceeding.

- **`mxlCreateFlowSynchronizationGroup(instance, &group)`**: Creates an empty group.

- **`mxlFlowSynchronizationGroupAddReader(group, reader)`**: Adds a flow reader to the group. For discrete readers, waits for full grains. For continuous readers, waits for samples.

- **`mxlFlowSynchronizationGroupAddPartialGrainReader(group, reader, minValidSlices)`**: Like `AddReader`, but for discrete flows only -- wakes up when at least `minValidSlices` are ready.

- **`mxlFlowSynchronizationGroupWaitForDataAt(group, timestamp, timeoutNs)`**: Blocks until data at the given TAI timestamp is available on **all** flows in the group. Converts the timestamp to an index for each flow based on its edit rate, then polls/waits each reader. Returns `MXL_STATUS_OK` when everyone is ready, `MXL_ERR_TIMEOUT` otherwise.

- **`mxlReleaseFlowSynchronizationGroup(instance, group)`**: Cleans up the group (doesn't release the individual readers).

---

### `time.h` -- The Clock Is the API

MXL's timing model is **TAI-based** (SMPTE ST 2059). Every flow is synchronized to absolute time, expressed as nanoseconds since the TAI epoch. Indices are simply derived from time via the edit rate.

#### **Index-Timestamp Conversions**

- **`mxlGetCurrentIndex(editRate)`**: What grain/sample index corresponds to "right now"? Reads the system clock, converts to TAI, divides by grain duration.

- **`mxlGetNsUntilIndex(index, editRate)`**: How many nanoseconds until index `index` starts? Use this to pace a write loop: compute the next index, sleep for the returned duration, then write.

- **`mxlTimestampToIndex(editRate, timestamp)`**: Convert a TAI timestamp to an index.

- **`mxlIndexToTimestamp(editRate, index)`**: Convert an index back to a TAI timestamp.

All math is exact integer arithmetic using the rational edit rate. No floating point, no rounding errors.

#### **Sleep and Clock Helpers**

- **`mxlSleepForNs(ns)`**: Block the calling thread for `ns` nanoseconds.

- **`mxlSleepUntil(timestamp)`**: Block until the absolute TAI time `timestamp`. Returns immediately if the timestamp is in the past.

- **`mxlGetTime()`**: Read the current TAI time from the system clock (uses `CLOCK_TAI` on Linux if available, falls back to `CLOCK_REALTIME`).

These functions abstract away platform-specific `clock_gettime` and `nanosleep` details, so your code stays portable.

---

## How It All Connects

1. **Create an instance** (`mxl.h`): Bind to a domain directory.
2. **Create a writer or reader** (`flow.h`): Parse a flow definition (NMOS JSON), allocate or open shared memory.
3. **Query the flow's config** (`flowinfo.h` via `flow.h`): Discover the format, rate, ring-buffer size.
4. **Branch on format** (`dataformat.h`):
   - Discrete (VIDEO/DATA)? Use the Grain API.
   - Continuous (AUDIO)? Use the Samples API.
5. **Synchronize with time** (`time.h`): Convert timestamps to indices, pace your loops, coordinate multiple flows.
6. **Handle errors** (`mxl.h`): Check every `mxlStatus` return, react to `TOO_LATE` / `TOO_EARLY` / `TIMEOUT`.
7. **Clean up** (`flow.h`, `mxl.h`): Release readers/writers, destroy the instance.

The API is **layered**: fundamental types (`platform.h`, `rational.h`, `dataformat.h`) support higher-level objects (`mxl.h` instance, `flowinfo.h` structures), which enable the actual I/O operations (`flow.h` readers and writers), all orchestrated by time (`time.h`).

---

## Why This Design?

**Portability**: The C ABI is the lingua franca. By exposing opaque handles and pure C types, MXL binds easily to Python, Rust, Go, or any language with an FFI.

**Zero-copy**: Pointers returned by readers and writers point **directly** into shared memory. No SDK-side buffer management, no hidden copies.

**Precision**: Rational arithmetic everywhere. Your 29.97 fps video stays frame-accurate for hours without drift.

**Composability**: The synchronization group lets you wait on multiple flows at once. The partial-grain API lets you pipeline work within a frame. The format classification helpers let you write generic code that works on video, audio, or data.

**Discoverability**: Every structure is documented inline. The header comments explain not just "what" but "why" and "how" -- this directory is both an API reference and a learning resource.

---

## In Summary

These seven headers define the **contract** between MXL and the outside world. They are the **public surface** -- stable, versioned, documented. Behind them (in `lib/internal/`) lives the implementation: POSIX file I/O, mmap, futex waits, domain scanning, flow parsing. But applications don't see any of that. They see these clean, portable, zero-copy primitives.

This is how you unlock shared-memory media exchange without sacrificing type safety, timing precision, or cross-language compatibility. This is the front door to MXL.
