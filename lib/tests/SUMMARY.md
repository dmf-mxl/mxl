# The Test Suite: How MXL Proves Itself Correct

This directory contains the **validation layer** for the MXL SDK -- a comprehensive test suite using Catch2 that exercises every major code path, verifies timing precision, validates concurrent behavior, and ensures correctness across edge cases. These tests don't just check "does it work?" -- they check "does it work **correctly** under realistic conditions?"

## The Story

Building a zero-copy, futex-synchronized, ring-buffer-based shared-memory media exchange system is hard. Getting it **right** is even harder. Race conditions hide in plain sight. Off-by-one errors lurk in index arithmetic. Timing drift accumulates over millions of frames. Concurrent readers and writers can step on each other in surprising ways.

The only way to know you got it right is to **test it relentlessly**.

This directory is the proof. Every test case is a scenario MXL must handle correctly. Some tests are simple ("can I create a flow?"). Some are subtle ("does a reader block correctly when waiting for a grain that's being written by a slow writer?"). Some are exhaustive ("do 30 million consecutive index-to-timestamp conversions round-trip perfectly?").

Together, they form a **contract**: if all tests pass, MXL behaves correctly. If a test fails, something is broken, and the suite tells you exactly what.

Let's walk through the tests and see what they validate.

---

## The Test Framework: Catch2 and Fixtures

MXL uses **Catch2**, a modern C++ testing framework with excellent diagnostics and minimal boilerplate.

### **`Utils.hpp`** -- Test Utilities and Fixtures

Provides:

- **`mxlDomainFixture`**: A RAII fixture that creates a temporary MXL domain directory (`/tmp/mxl-test-XXXXXX`) before each test and cleans it up afterward. Ensures test isolation (no test can pollute another's domain). Uses `mkdtemp()` for unique temp dir creation.

- **`readFile(path)`**: Loads NMOS flow definitions from `data/*.json` files. Test flows are stored as JSON files (`data/v210_flow.json`, `data/audio_flow.json`, `data/data_flow.json`) so tests use **real** NMOS flow descriptors, not hand-crafted test payloads.

- **`flowDirectoryExists(flowId)`**: Checks if a flow's directory (`${domain}/${flowId}.mxl-flow/`) exists on disk. Used to validate flow creation and garbage collection.

- **Helper macros**: `TEST_CASE_PERSISTENT_FIXTURE` wraps Catch2's `TEST_CASE` with fixture setup/teardown.

---

## The Test Files

### `test_instance.cpp` -- Instance Lifecycle and Flow Management

This suite validates the **top-level orchestration layer** -- how `Instance` manages readers, writers, and flow lifetime.

#### **Test: Flow Readers/Writers Caching**

**What it validates:**

- Creating multiple writers for different flows produces distinct writer handles.
- Creating multiple readers for the **same flow** returns the **same cached reader** (reference counting works).
- Readers and writers for the same flow are different objects.
- All readers/writers can be properly released.

**Why it matters:**

MXL's `Instance` caches readers to avoid duplicate memory mappings. If you call `mxlCreateFlowReader(instance, flowId, ...)` twice, you should get the same `FlowReader*` back (with refcount incremented). This test ensures that caching works and that writers are **not** cached (each writer is unique).

**Test logic:**

1. Create three flows (audio, video, metadata) via `mxlCreateFlowWriter()`.
2. Verify all three writers have distinct pointers.
3. Create readers for all three flows.
4. Create a **second reader** for the audio flow.
5. Verify the two audio readers have the **same pointer** (cached).
6. Verify readers for different flows have different pointers.
7. Release all readers/writers, verify no errors.

**Expected behavior:** Readers are reference-counted and cached per-flow-ID. Writers are not cached.

---

#### **Test: Flow Deletion on Writer Release**

**What it validates:**

- Flows persist as long as at least one writer holds a reference.
- When the last writer releases, the flow is deleted (directory removed).
- Multiple instances can safely share the same flow.

**Why it matters:**

MXL uses advisory locks for garbage collection. Flows should only be deleted when **no writers** are active. This test ensures that:
- Two instances can create writers for the same flow (second writer opens existing flow, doesn't create).
- First writer releases: flow **still exists** (second writer still active).
- Second writer releases: flow **deleted** (no more references).

**Test logic:**

1. Create two instances (`instanceA`, `instanceB`).
2. `instanceA` creates a writer for a v210 flow (flow is created).
3. `instanceB` creates a writer for the **same flow** (flow is opened, not re-created).
4. Verify flow directory exists.
5. Release `instanceA`'s writer, verify flow **still exists**.
6. Release `instanceB`'s writer, verify flow **no longer exists** (directory gone).

**Expected behavior:** Flow lifetime is tied to writer references. Garbage collection only deletes flows with zero active writers.

---

### `test_time.cpp` -- Timing Precision and Index Conversion

This suite validates MXL's **timing model** -- the math that converts between TAI timestamps and grain/sample indices.

#### **Test: Invalid Times**

**What it validates:**

- `mxlTimestampToIndex()` returns `MXL_UNDEFINED_INDEX` for invalid rates:
  - Null rate pointer
  - Zero numerator and zero denominator
  - Zero numerator only
  - Zero denominator only (division by zero)

**Why it matters:**

Invalid rates could cause crashes (division by zero) or incorrect timing. MXL must reject them gracefully.

---

#### **Test: Index 0 and 1**

**What it validates:**

- Index 0 corresponds to timestamp 0 (TAI epoch: 1970-01-01 00:00:00 TAI).
- Index 1 corresponds to one frame period later (e.g., 33.37ms for 29.97fps).
- Forward conversion (`mxlIndexToTimestamp`) matches expected values.
- Reverse conversion (`mxlTimestampToIndex`) is consistent.

**Why it matters:**

The epoch alignment is fundamental. If index 0 doesn't align with timestamp 0, all timing is off. This test ensures the math is correct at the boundary.

**Test logic:**

1. Define rate as `{30000, 1001}` (29.97fps).
2. Compute expected timestamps for index 0 and 1 using exact integer math.
3. Verify `mxlTimestampToIndex(rate, timestamp)` returns correct indices.
4. Verify `mxlIndexToTimestamp(rate, index)` returns correct timestamps.

**Expected behavior:** Perfect roundtrip at epoch boundary.

---

#### **Test: TAI Epoch**

**What it validates:**

- Timestamp 0 converts to Unix epoch (1970-01-01 00:00:00 UTC).
- `gmtime_r()` on timestamp 0 returns year=1970, month=January, day=1, hour=0, min=0, sec=0.

**Why it matters:**

SMPTE ST 2059 TAI epoch is aligned with Unix epoch. This test confirms the SDK's understanding of the epoch matches POSIX's.

---

#### **Test: Index/Timestamp Roundtrip (Current)**

**What it validates:**

- Current system time → index → timestamp roundtrip is consistent.
- The difference between original and roundtrip timestamp is less than one frame period.
- `mxlGetNsUntilIndex()` returns positive value for future indices (used for sleep/wait).

**Why it matters:**

Real applications will call `mxlGetCurrentIndex()` to determine which grain to write next. This test ensures that the current time → index conversion is accurate and that sleep calculations work.

**Test logic:**

1. Get current TAI time via `mxlGetTime()`.
2. Convert to index via `mxlTimestampToIndex()`.
3. Convert back to timestamp via `mxlIndexToTimestamp()`.
4. Verify difference is less than one frame duration (~33.37ms for 29.97fps).
5. Verify `mxlGetNsUntilIndex(currentIndex + 33, rate)` returns positive value (future index).

**Expected behavior:** Roundtrip precision within one frame period. Sleep calculations work for future indices.

---

#### **Test: Exhaustive Roundtrip (30 Million Indices)**

**What it validates:**

- Perfect roundtrip consistency for 30 million consecutive indices.
- Each index converts to a unique timestamp.
- Each timestamp converts back to the original index.
- No rounding errors or drift over extended ranges (~11.6 days of continuous video at 29.97fps).

**Why it matters:**

Long-running systems (e.g., 24/7 broadcast) accumulate millions of frames. Any rounding error compounds over time, causing drift. This test ensures that MXL's integer arithmetic remains frame-accurate over extended operation.

**Test logic:**

Loop from index 0 to 30,000,000:
1. Convert index to timestamp.
2. Convert timestamp back to index.
3. Verify roundtrip index == original index.

**Expected behavior:** All 30 million indices round-trip perfectly. No drift.

---

### `test_flows.cpp` -- Core Flow Operations

This is the **main test suite** for flow reader/writer functionality. It covers video, audio, and data flows, grain and sample I/O, and edge cases.

#### **Test: Video Flow -- Create/Destroy**

**What it validates:**

- Creating a v210 video flow from NMOS JSON descriptor.
- Flow becomes active when writer is created (`mxlIsFlowActive()` returns true).
- Opening a grain for writing allocates proper buffer size.
- Writing invalid grains (marked with `MXL_GRAIN_FLAG_INVALID`).
- Reading grains back with zero-copy access.
- Grain metadata (index, size, flags) is preserved.
- Buffer contents are accessible (marks at start/end verified).
- Flow cleanup releases all resources.

**Why it matters:**

This is the **happy path** for video flows. If this test fails, basic video I/O is broken.

**Test logic:**

1. Create two instances (reader and writer) sharing the same domain.
2. Create a v210 flow (1920x1080 59.94fps) via `mxlCreateFlowWriter()`.
3. Verify flow is active via `mxlIsFlowActive()`.
4. Compute grain index for current TAI time via `mxlGetCurrentIndex()`.
5. Open grain for writing via `mxlFlowWriterOpenGrain()`.
6. Verify grain size matches expected V210 payload size (`width * height * V210_bytes_per_pixel`).
7. Write marks at start/end of grain buffer (`buffer[0] = 0xCA`, `buffer[size-1] = 0xFE`).
8. Mark grain as invalid (`gInfo.flags |= MXL_GRAIN_FLAG_INVALID`).
9. Commit grain via `mxlFlowWriterCommitGrain()`.
10. Read grain back via `mxlFlowReaderGetGrain()` (blocks until available, up to 16ms timeout).
11. Verify flags are preserved (`gInfo.flags == MXL_GRAIN_FLAG_INVALID`).
12. Verify marks are still present (`buffer[0] == 0xCA`, `buffer[size-1] == 0xFE`).
13. Release readers/writers, destroy instances.

**Expected behavior:** Zero-copy read/write. Metadata preserved. Invalid flag propagates to reader.

---

#### **Other Tests in `test_flows.cpp`**

The file contains many more tests covering:

- **Audio Flow**: Create/destroy continuous audio flow, write/read samples.
- **Data Flow**: Create/destroy ST 291 ancillary data flow (discrete, like video, but variable-length grains).
- **Ring Buffer Wraparound**: Verify that grains/samples wrap correctly when ring buffer fills.
- **Multi-Reader/Writer**: Multiple readers on same flow, multiple instances sharing same flow.
- **Invalid Grain Handling**: Readers correctly interpret `MXL_GRAIN_FLAG_INVALID`.
- **Flow Activation/Deactivation**: `mxlIsFlowActive()` reflects writer presence.
- **PCAP File Generation** (Linux only): Generate network packet captures for validating network transmission (part of Fabrics layer testing).

---

### `test_flows_timing.cpp` -- Timing and Synchronization

This suite validates **concurrent behavior** and **blocking reads** with realistic timing constraints.

#### **Test: Video Flow -- Wait for Grain Availability**

**What it validates:**

- Reader blocks correctly when waiting for a grain that hasn't been written yet.
- Writer produces grains at real-time rate (~33ms per frame @ 29.97fps).
- Reader successfully retrieves grain after ~100ms wait (3 frames of latency).
- Grain data integrity (embedded grain index verified).
- Thread-safe concurrent access (writer and reader running in separate threads).

**Why it matters:**

This simulates a **real-time media pipeline** where a reader is slightly ahead of a writer. The reader must block (not spin) until the writer catches up. This test validates the futex wait/wake mechanism.

**Test logic:**

1. Create two instances (reader and writer).
2. Create a v210 flow.
3. Compute current grain index (`readerGrainIndex`) via `mxlGetCurrentIndex()`.
4. Spawn writer thread:
   - Start at `readerGrainIndex - 3` (3 grains behind).
   - Write grains in a loop, incrementing index each time.
   - Embed the grain index in the grain payload (for verification).
   - Sleep for one frame duration between grains (simulates real-time rate).
5. On main thread: call `mxlFlowReaderGetGrain(reader, readerGrainIndex, 1s timeout)`.
6. Reader blocks (futex wait) until writer produces grain at `readerGrainIndex`.
7. Reader wakes up, retrieves grain, verifies embedded index matches expected.
8. Wait for writer thread to finish.
9. Clean up.

**Expected behavior:** Reader blocks for ~100ms (time for writer to produce 3 grains), then successfully retrieves grain. No timeout. Grain data is correct.

---

### `fabrics/ofi/` -- Fabrics Networking Layer Tests

These tests validate the **Fabrics layer** -- MXL's extension for remote media exchange over RDMA (Remote Direct Memory Access) using libfabric (OpenFabrics Interface).

Files:

- **`test_Provider.cpp`**: Tests libfabric provider initialization and capability queries.
- **`test_Domain.cpp`**: Tests fabric domain creation and resource management.
- **`test_Address.cpp`**: Tests address vector management (for RDMA endpoint addressing).
- **`test_Region.cpp`**: Tests memory region registration (pinning memory for RDMA access).

**Note:** These tests are separate from the core MXL flow tests because they require RDMA-capable hardware or software RDMA emulation (e.g., `rxe` on Linux). The Fabrics layer is an optional extension for networked MXL deployments.

---

## Test Coverage: What's Validated

### **API Correctness**

- All public C API functions (`mxl.h`, `flow.h`, `time.h`) are exercised.
- Return codes are checked (`MXL_STATUS_OK` vs. error codes).
- Out-parameters are validated (pointers non-null, values correct).

### **Timing Precision**

- Index-to-timestamp conversions are frame-accurate (no drift over 30 million frames).
- TAI epoch alignment is correct.
- Sleep/wait calculations work for future indices.
- Invalid rates are rejected gracefully.

### **Flow Lifecycle**

- Flows are created correctly (directory structure, JSON descriptor, shared memory files).
- Flows are opened correctly (existing flows mapped, not re-created).
- Flows are deleted correctly (when last writer releases).
- Garbage collection respects advisory locks (active flows not deleted).

### **Zero-Copy I/O**

- Grain payloads are writable by writers, readable by readers.
- Sample buffers are writable by writers, readable by readers.
- No copying detected (tests verify pointers point directly into shared memory).

### **Metadata Preservation**

- Grain flags (e.g., `MXL_GRAIN_FLAG_INVALID`) propagate from writer to reader.
- Grain indices are preserved.
- Flow configuration info (format, dimensions, rate) is accessible to readers.

### **Concurrent Behavior**

- Multiple readers can access the same flow simultaneously.
- Multiple instances can share the same domain.
- Writers and readers can operate concurrently (different threads, different processes).
- Blocking reads work correctly (futex wait/wake, no spinning).

### **Edge Cases**

- Invalid grains (marked with `MXL_GRAIN_FLAG_INVALID`) are handled correctly.
- Ring buffer wraparound works (grains/samples wrap correctly when buffer fills).
- Out-of-range reads return correct error codes (`MXL_ERR_OUT_OF_RANGE_TOO_LATE` / `TOO_EARLY`).
- Timeouts work correctly (blocking reads return `MXL_ERR_TIMEOUT` when data doesn't arrive in time).

### **Reference Counting**

- Readers are cached within an instance (same pointer returned on duplicate `mxlCreateFlowReader()` calls).
- Writers are not cached (each `mxlCreateFlowWriter()` call returns a unique pointer).
- Release calls decrement refcounts correctly.
- Resources are freed when refcount hits zero.

### **Flow Formats**

- Video flows (V210, 1920x1080, 59.94fps) work correctly.
- Audio flows (48kHz, stereo, float32) work correctly.
- Data flows (ST 291 ancillary data) work correctly.

---

## Test Execution: How to Run

MXL uses CMake's `ctest` for test execution. From the build directory:

```bash
# Run all tests
ctest

# Run tests with verbose output
ctest -V

# Run tests matching a pattern
ctest -R "flows"      # Run only flow tests
ctest -R "time"       # Run only timing tests
ctest -R "instance"   # Run only instance tests

# Run a specific test file
./lib/tests/test_flows
./lib/tests/test_time
./lib/tests/test_instance
```

Each test file is an independent executable built from a single `.cpp` file. Catch2 provides test discovery (no manual registration needed).

---

## Test Data: NMOS Flow Descriptors

Test flows are defined as JSON files in the `data/` directory:

- **`data/v210_flow.json`**: 1920x1080 59.94fps V210 video flow (YCbCr 4:2:2 10-bit).
- **`data/audio_flow.json`**: 48kHz stereo float32 audio flow.
- **`data/data_flow.json`**: ST 291 ancillary data flow (metadata/captions).

These are **real NMOS IS-04 Flow Resource descriptors** (same format used in production NMOS systems). Tests read these files and pass them to `mxlCreateFlowWriter()`, ensuring that MXL handles realistic flow definitions.

---

## Why This Test Strategy?

**Black-box testing:** Tests use the public C API exclusively. They don't peek into internal implementation details. This ensures that applications will work the same way.

**Realistic scenarios:** Tests simulate real-world use cases (video capture/playout, audio processing, concurrent readers/writers). Not just toy examples.

**Catch2 framework:** Modern C++ testing with excellent diagnostics. When a test fails, Catch2 tells you **exactly** what went wrong (expected value, actual value, line number).

**Fixture isolation:** Each test gets a clean temporary domain directory. No test can pollute another's state. Failures are reproducible.

**Comprehensive coverage:** Tests cover happy paths, edge cases, concurrent scenarios, error handling, timing precision, and long-duration stability.

**No flaky tests:** Tests don't depend on wall-clock time (except for relative sleeps). They use controlled timing (grain indices, explicit delays). No spurious failures due to system load.

**Fast execution:** Most tests complete in milliseconds. The exhaustive timing test (30 million indices) takes a few seconds. Full suite runs in under a minute.

---

## In Summary

This directory is the **contract** between MXL and its users. It defines what MXL **must** do correctly:

- Create flows from NMOS JSON descriptors.
- Provide zero-copy read/write access to media data.
- Preserve metadata (flags, indices, formats).
- Block correctly when waiting for data (futex waits, not polling).
- Handle concurrent readers/writers safely.
- Convert timestamps to indices with frame-accurate precision.
- Manage flow lifecycle correctly (creation, opening, deletion).
- Reference-count readers and writers correctly.
- Handle edge cases gracefully (invalid rates, out-of-range reads, timeouts).

If all tests pass, MXL works. If a test fails, something is broken, and the diagnostics tell you what.

This is how MXL proves itself correct. Test by test. Scenario by scenario. Edge case by edge case.

The code is the implementation. The tests are the proof.
