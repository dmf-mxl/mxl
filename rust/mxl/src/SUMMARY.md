# The Safe Rust API: A Story of Transformation

This folder contains the heart of the Rust MXL experience: the safe, idiomatic API that transforms raw C pointers into beautiful, memory-safe Rust abstractions. It's the story of how chaos becomes order, how danger becomes safety, and how foreign function interfaces become native Rust.

## The Foundation

**lib.rs** is where the story begins. It's the front door to the entire crate, defining the public API and establishing the conceptual model: MXL is a shared-memory media exchange system where **instances** connect to **domains**, which contain **flows** of media data. Flows come in two flavors: discrete (grains for video/data) and continuous (samples for audio). The file orchestrates all the modules and exports the public API, making MXL approachable for Rust developers.

**api.rs** handles the first critical step: loading the MXL C library from disk at runtime. Using `libloading`, it dynamically opens the shared object file and binds all the C functions. The `MxlApi` type alias represents the loaded function table, and `MxlApiHandle` wraps it in an `Arc` for thread-safe sharing. This is where the bridge between C and Rust is first established.

**config.rs** provides build-time path resolution. It includes compiler-generated constants (`MXL_REPO_ROOT` and `MXL_BUILD_DIR`) and exposes functions to locate the MXL shared library and repository root. This allows examples and tests to find the library without hardcoding paths, adapting to both normal builds and pre-built library scenarios via the `mxl-not-built` feature.

**error.rs** is the error translator. It takes raw C integer status codes (like `MXL_STATUS_OK`, `MXL_ERR_TIMEOUT`, `MXL_ERR_FLOW_NOT_FOUND`) and converts them into Rust's `Result` type with strongly-typed error enums. The `Error` enum covers every failure mode: timeouts, invalid arguments, missing flows, string conversion failures, and library loading errors. The `from_status` function is the key transformer, mapping C codes to Rust errors.

**instance.rs** implements `MxlInstance`, the main entry point. An instance represents a connection to an MXL domain (a tmpfs directory containing shared memory). The internal `InstanceContext` holds the raw C API handle and instance pointer, separated from `MxlInstance` to enable cloning and shared ownership across threads via `Arc`. The instance is marked `Send + Sync` because the MXL C API guarantees thread-safety at this level. It provides methods to create readers and writers, query timing (TAI timestamps), convert between indices and timestamps, and sleep with high-precision timing. The `create_flow_reader` helper function is also here, used by both the instance and internally by writers.

## The Flow Abstraction

**flow.rs** defines the flow type system. `DataFormat` classifies flows as Video, Audio, Data, or Unspecified. `FlowConfigInfo` wraps static configuration (format, rate, dimensions), while `FlowRuntimeInfo` tracks dynamic state (head index, last access times). The `CommonFlowConfigInfo` wrapper provides safe access to fields shared by all flow types. The `is_discrete_data_format` helper determines whether a flow uses grain-based (discrete) or sample-based (continuous) delivery.

**flow/flowdef.rs** contains Rust structures for parsing JSON flow definitions that follow the NMOS IS-04 schema. `FlowDef` represents the complete definition with metadata like ID, label, and format. `FlowDefDetails` is an enum that dispatches to format-specific structures: `FlowDefVideo` (with frame dimensions, rate, colorspace) or `FlowDefAudio` (with sample rate, channel count, bit depth). The `Rate` struct represents rational numbers for frame and sample rates.

**flow/reader.rs** implements `FlowReader`, the generic reader returned by `create_flow_reader`. It's type-erased and must be converted to either `GrainReader` or `SamplesReader` based on the flow type. The conversion methods (`to_grain_reader`, `to_samples_reader`) consume the generic reader and return a typed one, nulling out the internal pointer to prevent double-release. Helper functions like `get_flow_info`, `get_config_info`, and `get_runtime_info` query metadata from the C API. The reader is `Send` but not `Sync` (not thread-safe).

**flow/writer.rs** implements `FlowWriter`, the generic writer returned by `create_flow_writer`. Like the reader, it must be converted to a typed writer. The `get_flow_type` method is an interesting workaround: since the C API doesn't provide a direct way to query flow type from a writer, it temporarily creates a reader to fetch the metadata. The writer is also `Send` but not `Sync`.

## The Grain System (Discrete Media)

**grain.rs** is the module entry point for discrete media. It declares submodules and doesn't contain implementation code.

**grain/data.rs** defines `GrainData`, a zero-copy view of a grain's payload. It holds a borrowed slice (`&[u8]`) pointing directly into shared memory, along with metadata like total size and flags. The lifetime `'a` ties it to the reader that produced it. `OwnedGrainData` provides an owned copy that can outlive the reader. These types enable zero-copy reading: the grain data lives in shared memory and is viewed without allocation.

**grain/reader.rs** implements `GrainReader` for reading video frames and data packets. The `get_complete_grain` method performs blocking reads with timeout, looping until all slices are valid. The `get_grain_non_blocking` method returns immediately, potentially with partial data. Both methods return `GrainData` views with lifetimes tied to the reader. The reader queries flow metadata (`get_info`, `get_config_info`, `get_runtime_info`) and automatically releases the C handle on drop.

**grain/write_access.rs** implements `GrainWriteAccess`, an RAII-protected write session. It provides mutable access to a grain's payload buffer via `payload_mut()`, returning `&mut [u8]` for zero-copy writing. The session must be explicitly committed with `commit(valid_slices)` to publish the grain, or explicitly canceled with `cancel()`. If neither happens, the drop implementation automatically cancels the grain, ensuring consistency even during panics. This is Rust's RAII pattern at work: resources are tied to object lifetimes.

**grain/writer.rs** implements `GrainWriter` for writing discrete media. The `open_grain` method opens a write session at a specific index, returning a `GrainWriteAccess` tied to the writer's lifetime. The writer can be explicitly destroyed with `destroy()` or will automatically release on drop.

## The Samples System (Continuous Media)

**samples.rs** is the module entry point for continuous media, parallel to `grain.rs`.

**samples/data.rs** defines `SamplesData`, a zero-copy view of multi-channel audio. Unlike grains, samples may be split into two fragments per channel if the ring buffer wraps around. The `channel_data` method returns a tuple of two byte slices for each channel. `OwnedSamplesData` provides an owned copy with fragments concatenated per channel.

**samples/reader.rs** implements `SamplesReader` for reading audio streams. The `get_samples` method performs blocking reads with timeout, fetching a batch of samples at a specific index. The `get_samples_non_blocking` method returns immediately. Both return `SamplesData` views with lifetimes tied to the reader. The reader provides the same metadata query methods as `GrainReader`.

**samples/write_access.rs** implements `SamplesWriteAccess`, the RAII-protected write session for audio. It provides mutable access to multi-channel buffers via `channel_data_mut`, which returns a tuple of two mutable byte slices per channel (for ring wrapping). The session must be explicitly committed or canceled, with automatic cancellation on drop ensuring consistency.

**samples/writer.rs** implements `SamplesWriter` for writing audio. The `open_samples` method opens a write session at a specific index for a batch of samples, returning a `SamplesWriteAccess` tied to the writer's lifetime.

## The Architecture

The flow of data through this API follows a clear pattern:

1. **Load**: `load_api` dynamically loads the C library
2. **Connect**: `MxlInstance::new` connects to a domain
3. **Create/Open**: Create writers with flow definitions or open readers by flow ID
4. **Convert**: Convert generic readers/writers to typed ones (Grain or Samples)
5. **Access**: Open RAII-protected sessions for zero-copy read/write
6. **Transfer**: Read or write directly to/from shared memory via byte slices
7. **Commit**: Explicitly commit writes or rely on automatic cleanup
8. **Release**: Drop handlers automatically release C resources

Every unsafe operation is carefully wrapped. Every C pointer is hidden behind safe abstractions. Every error code becomes a Result. Every resource is tied to an RAII lifetime. This is the transformation: from raw, unsafe C to safe, idiomatic Rust.
