<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# MXL : Media eXchange Layer

[![Build Pipeline](https://github.com/dmf-mxl/mxl/actions/workflows/build.yml/badge.svg)](https://github.com/dmf-mxl/mxl/actions/workflows/build.yml)
[![GitHub release](https://img.shields.io/github/v/release/dmf-mxl/mxl)](https://github.com/dmf-mxl/mxl/releases)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE.txt)
[![Issues](https://img.shields.io/github/issues/dmf-mxl/mxl)](https://github.com/dmf-mxl/mxl/issues)

---

## What is MXL?

**MXL is a zero-copy media exchange SDK** for high-performance broadcast workflows. It enables video, audio, and ancillary data to flow between media functions (ingest, processing, playout) without serialization, packetization, or memory copies.

**Key Value Propositions:**

- **Zero-copy shared memory**: Video frames, audio buffers, and data grains live in tmpfs-backed ring buffers. Producers write directly; consumers read directly. No CPU overhead from memcpy.
- **Time-accurate indexing**: Every grain and sample is indexed by absolute TAI timestamp (SMPTE ST 2059 epoch). Rational arithmetic eliminates drift over hours of operation.
- **Container-native**: Designed for Kubernetes, Docker, and bare-metal deployments. Flows are POSIX files with standard permissions, security, and observability.
- **Interoperable by design**: Implements the [EBU Dynamic Media Facility Reference Architecture](https://tech.ebu.ch/dmf/ra) exchange layer. C API with Rust bindings. Open-source Apache 2.0.

**Who should use MXL?**

- Broadcast engineers building software-defined production systems
- Developers integrating heterogeneous media functions (capture, transcode, composite, encode)
- Platform architects deploying cloud or hybrid broadcast infrastructure
- Anyone replacing hardware SDI routers with flexible, container-based media routing

---

## Table of Contents

### Getting Started

- [Quick Start (C)](#quick-start-c)
- [Quick Start (Rust)](#quick-start-rust)
- [Building the SDK](docs/Building.md)
- [Deployment Options](#deployment)
- [Hands-On Workshop](https://github.com/cbcrc/mxl-hands-on)

### Core Concepts

- [Media Types and Formats](#media-types-and-formats)
- [Grain Types and Timing Model](#grain-types-and-timing-model)
- [Ring Buffers and Indexing](#ring-buffers-and-indexing)
- [Origin Timestamps (OTS)](#origin-timestamps-ots)

### Documentation

- [Architecture Overview](docs/Architecture.md)
- [SDK Usage Guide](docs/Usage.md)
- [Timing Model](docs/Timing.md)
- [Addressability](docs/Addressability.md)
- [Configuration](docs/Configuration.md)
- [Tools and Utilities](docs/Tools.md)

### API Reference

- [Public C API Summary](lib/include/mxl/SUMMARY.md)
- [Internal C++ API Summary](lib/internal/include/mxl-internal/SUMMARY.md)
- [Fabrics API Summary](lib/fabrics/SUMMARY.md)
- [Rust Bindings Summary](rust/mxl/src/SUMMARY.md)
- [Rust FFI Summary](rust/mxl-sys/src/SUMMARY.md)
- [GStreamer Plugin Summary](rust/gst-mxl-rs/src/SUMMARY.md)

### Testing and Examples

- [Test Suite Summary](lib/tests/SUMMARY.md)
- [Rust Examples Summary](rust/mxl/examples/SUMMARY.md)
- [Deployment Examples](examples/README.md)

### Tools

- [Tools Overview](tools/SUMMARY.md)
- [mxl-info (Flow Inspector)](tools/mxl-info/SUMMARY.md)
- [mxl-gst (GStreamer Pipelines)](tools/mxl-gst/SUMMARY.md)
- [mxl-fabrics-demo (Networking Demo)](tools/mxl-fabrics-demo/SUMMARY.md)

### Governance and Community

- [Governance Principles](GOVERNANCE/GOVERNANCE.md)
- [Technical Charter](GOVERNANCE/CHARTER.pdf)
- [How to Contribute](CONTRIBUTING.md)
- [Security Policy](SECURITY.md)
- [Code of Conduct](CODE_OF_CONDUCT.md)

---

## Media Types and Formats

MXL natively supports professional broadcast media formats with precise specifications:

### VIDEO

**v210 Format** (10-bit 4:2:2 YCbCr)
- Packed 10-bit samples, 48 bytes per 16 pixels
- Compatible with SMPTE ST 274, 296, 2048, and ST 2110-20
- Frame sizes: SD (720x486/576), HD (1920x1080), UHD (3840x2160), 4K DCI (4096x2160)
- Interlace modes: progressive, top-field-first, bottom-field-first
- Slice-based writes enable sub-frame latency (line-by-line processing)

**v210a Format** (v210 + Alpha/Key Channel)
- Dual-plane format: fill (YCbCr v210) + key (10-bit alpha)
- Straight alpha compositing (not premultiplied)
- Video range alpha (64-940), matches SMPTE ST 2110-40 key signal
- Used for graphics overlays, downstream keyers, virtual production

**Discrete Grain Model**: Video flows are organized as discrete frames. Each frame is a grain occupying one slot in the ring buffer.

### AUDIO

**float32 Format** (32-bit floating-point PCM)
- Per-channel ring buffers, native alignment for SIMD operations
- Sample rates: 48 kHz, 96 kHz, or any rational rate
- Channel counts: mono to 64+ channels per flow
- Compatible with AES67, SMPTE ST 2110-30, and Dante

**Continuous Sample Model**: Audio flows are continuous streams. Readers access arbitrary time windows (e.g., "give me 512 samples ending at index N"). Ring buffers automatically handle wrap-around.

### ANCILLARY DATA

**SMPTE ST 291 Format** (VANC/HANC data packets)
- Formatted per RFC 8331 (RTP Payload Format for SMPTE ST 291)
- Carries timecode (ST 12-2), captions (CEA-608/708), camera metadata
- Aligned to video frame boundaries (one data grain per video grain)

**Discrete Grain Model**: Data flows are discrete blocks, synchronized frame-by-frame with video.

### Format Detection

All formats are declared in NMOS IS-04 Flow Resource JSON (`urn:x-nmos:format:video`, `urn:x-nmos:format:audio`, `urn:x-nmos:format:data`). The SDK provides helpers to classify flows as discrete (video/data) or continuous (audio), ensuring you call the correct API functions.

---

## Grain Types and Timing Model

MXL uses two fundamentally different data models, matched to the nature of the media.

### Discrete Grains (VIDEO, DATA)

**What is a Grain?**
A grain is a self-contained media unit: a video frame, a data block, or an ancillary packet. Each grain occupies one slot in the ring buffer.

**Indexing**:
Grains are numbered from zero at the SMPTE ST 2059 TAI epoch (1970-01-01 00:00:00 TAI). The grain index is:

```
grainIndex = timestamp_ns / (1_000_000_000 / frame_rate)
```

For 50 fps video, grain 0 = 0 ns, grain 1 = 20,000,000 ns, grain 2 = 40,000,000 ns.

**Ring Buffer Wrap**:
A ring buffer with `grainCount=100` wraps every 100 grains. Ring buffer index = `grainIndex % grainCount`. Grain 0 and grain 100 both occupy ring slot 0.

**Partial Writes**:
Video writers can commit scan lines incrementally. A 1080-line HD frame is 1080 slices. The writer commits slice 0-99, then 100-199, etc. Readers waiting with `minValidSlices=100` wake up after the first 100 lines, enabling line-based pipelines.

### Continuous Samples (AUDIO)

**What is a Sample?**
Audio is a never-ending stream of float32 values, one per channel. There are no "grain boundaries." The ring buffer is a circular per-channel array of samples.

**Indexing**:
Samples are numbered from zero at the TAI epoch. For 48 kHz audio:

```
sampleIndex = timestamp_ns / (1_000_000_000 / 48000)
```

Sample 0 = 0 ns, sample 1 = 20,833 ns (approx).

**Ring Buffer Wrap**:
A ring buffer with `bufferLength=96000` samples wraps every 2 seconds (at 48 kHz). Ring index = `sampleIndex % bufferLength`.

**Window Reads**:
The API reads ranges: "give me 512 samples per channel ending at index N." If the range wraps around the ring, the SDK returns two memory fragments. Your code iterates both.

---

## Ring Buffers and Indexing

Every flow is a **ring buffer** stored in shared memory (tmpfs). The ring size is chosen to balance history depth against memory usage.

### TAI Timestamps and the SMPTE ST 2059 Epoch

MXL timestamps are **absolute TAI time** (International Atomic Time), measured in nanoseconds since the SMPTE ST 2059-1 epoch (1970-01-01 00:00:00 TAI).

**Why TAI?**
- No leap seconds (unlike UTC). Time always advances uniformly.
- Traceable to GPS, atomic clocks, and NTP Stratum 0 sources.
- Supported by SMPTE ST 2110, PTP grandmasters, and cloud time sync services.

**Time Sources**:
- Single host: Any stable clock (NTP, PTP, system `CLOCK_TAI`).
- Multi-host: Synchronized clocks that do not drift (PTP, GPS-locked NTP, cloud provider time services). Jitter is acceptable; MXL timestamps describe it.

### Origin Timestamps (OTS)

**What is an Origin Timestamp?**
The OTS is the **capture time** of a media grain, typically unrolled from RTP timestamps or hardware vsync signals. It tells readers when the content was originally produced, independent of transport latency or jitter.

**Why OTS Matters**:
A video frame captured at `T=100ms` may arrive at a decoder at `T=150ms` due to network delay. Without OTS, the decoder would write the frame at index(150ms), misaligning it with audio captured at 100ms. With OTS, the decoder writes at index(100ms), preserving A/V sync.

**How OTS is Used**:
- **2110 Receivers**: Unroll the RTP timestamp from the first packet of a frame, convert to TAI, compute `grainIndex = OTS / grainDuration`, write to that index.
- **Live Generators**: Use the current TAI time as OTS (`mxlGetCurrentIndex()`).
- **File Players**: Derive OTS from edit unit timecode or PTS.

**OTS Preservation Across Fabrics**:
When MXL replicates a flow between hosts using the Fabrics API (RDMA, EFA), the grain indices and OTS are preserved byte-for-byte. The remote writer writes to the same indices as the source writer.

---

## Quick Start (C)

### Writing a Video Flow

```c
#include <mxl/mxl.h>
#include <mxl/flow.h>
#include <mxl/time.h>

int main(void) {
    // Create an MXL instance bound to /dev/shm/mxl
    mxlInstance* instance = NULL;
    mxlCreateInstance("/dev/shm/mxl", NULL, &instance);

    // Define a 1080p50 v210 flow (NMOS JSON)
    const char* flowDefJson = "{"
        "\"id\": \"12345678-1234-1234-1234-123456789012\","
        "\"format\": \"urn:x-nmos:format:video\","
        "\"media_type\": \"video/raw\","
        "\"components\": [{\"name\": \"Y\", \"width\": 1920, \"height\": 1080, \"bit_depth\": 10}],"
        "\"frame_width\": 1920,"
        "\"frame_height\": 1080,"
        "\"interlace_mode\": \"progressive\","
        "\"colorspace\": \"BT709\","
        "\"grain_rate\": {\"numerator\": 50, \"denominator\": 1}"
        "}";

    mxlFlowWriter* writer = NULL;
    mxlFlowConfigInfo configInfo;
    bool created = false;
    mxlCreateFlowWriter(instance, flowDefJson, NULL, &writer, &configInfo, &created);

    // Write 100 frames at 50 fps
    mxlRational frameRate = {50, 1};
    for (int i = 0; i < 100; i++) {
        uint64_t currentIndex = mxlGetCurrentIndex(frameRate);

        // Open grain slot, get writable payload pointer
        mxlGrainInfo* grainInfo = NULL;
        void* payload = NULL;
        mxlFlowWriterOpenGrain(writer, currentIndex, &grainInfo, &payload);

        // Write video data (v210 pixels) to payload
        // ... (fill payload with 1920x1080 v210 data) ...

        // Commit the grain, wake readers
        mxlFlowWriterCommitGrain(writer, grainInfo);

        // Sleep until next frame
        mxlSleepForNs(mxlGetNsUntilIndex(currentIndex + 1, frameRate));
    }

    // Cleanup
    mxlReleaseFlowWriter(instance, writer);
    mxlDestroyInstance(instance);
    return 0;
}
```

### Reading a Video Flow

```c
#include <mxl/mxl.h>
#include <mxl/flow.h>

int main(void) {
    mxlInstance* instance = NULL;
    mxlCreateInstance("/dev/shm/mxl", NULL, &instance);

    // Open existing flow by UUID
    const char flowId[16] = {0x12, 0x34, 0x56, 0x78, /* ... */};
    mxlFlowReader* reader = NULL;
    mxlCreateFlowReader(instance, flowId, NULL, &reader);

    // Get current head position
    mxlFlowRuntimeInfo runtimeInfo;
    mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo);
    uint64_t headIndex = runtimeInfo.headIndex;

    // Read 10 frames
    for (int i = 0; i < 10; i++) {
        uint64_t readIndex = headIndex + i + 1;

        // Wait up to 100ms for the frame
        const mxlGrainInfo* grainInfo = NULL;
        const void* payload = NULL;
        mxlStatus status = mxlFlowReaderGetGrain(reader, readIndex, 100000000, &grainInfo, &payload);

        if (status == MXL_STATUS_OK) {
            // Process video frame (zero-copy, payload points to shared memory)
            // ... (read v210 pixels from payload) ...
        } else if (status == MXL_ERR_TIMEOUT) {
            // Writer stalled
            break;
        }
    }

    mxlReleaseFlowReader(instance, reader);
    mxlDestroyInstance(instance);
    return 0;
}
```

### Writing and Reading Audio

```c
// Audio writer: open 512 samples per channel, write float32 data
mxlWrappedMultiBufferSlice slices;
mxlFlowWriterOpenSamples(writer, startIndex, 512, &slices);
// Fill slices.base.fragments[0/1].pointer with float32 samples...
mxlFlowWriterCommitSamples(writer);

// Audio reader: read 512 samples per channel ending at targetIndex
mxlWrappedMultiBufferSlice slices;
mxlFlowReaderGetSamples(reader, targetIndex, 512, 100000000, &slices);
// Read float32 samples from slices.base.fragments[0/1].pointer...
```

---

## Quick Start (Rust)

Add MXL to your `Cargo.toml`:

```toml
[dependencies]
mxl = { git = "https://github.com/dmf-mxl/mxl", branch = "main" }
```

### Writing a Video Flow (Rust)

```rust
use mxl::{Instance, FlowWriter, Rational, get_current_index};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create instance
    let instance = Instance::new("/dev/shm/mxl", None)?;

    // Define flow (NMOS JSON)
    let flow_def = r#"{
        "id": "12345678-1234-1234-1234-123456789012",
        "format": "urn:x-nmos:format:video",
        "frame_width": 1920,
        "frame_height": 1080,
        "grain_rate": {"numerator": 50, "denominator": 1}
    }"#;

    let (writer, _config, _created) = FlowWriter::new(&instance, flow_def, None)?;
    let frame_rate = Rational::new(50, 1);

    // Write 100 frames
    for _ in 0..100 {
        let current_index = get_current_index(frame_rate);

        let mut grain = writer.open_grain(current_index)?;
        // Write v210 data to grain.payload_mut()
        grain.commit()?;

        std::thread::sleep(Duration::from_millis(20)); // 50 fps = 20ms/frame
    }

    Ok(())
}
```

### Reading a Video Flow (Rust)

```rust
use mxl::{Instance, FlowReader, Uuid};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let instance = Instance::new("/dev/shm/mxl", None)?;
    let flow_id = Uuid::parse_str("12345678-1234-1234-1234-123456789012")?;
    let reader = FlowReader::new(&instance, &flow_id, None)?;

    let runtime_info = reader.get_runtime_info()?;
    let head_index = runtime_info.head_index;

    // Read 10 frames
    for i in 0..10 {
        let read_index = head_index + i + 1;
        match reader.get_grain(read_index, Some(Duration::from_millis(100))) {
            Ok(grain) => {
                // Process grain.payload() - zero-copy slice into shared memory
                println!("Read grain {} with {} bytes", read_index, grain.info().grain_size);
            }
            Err(e) => {
                eprintln!("Error reading grain: {}", e);
                break;
            }
        }
    }

    Ok(())
}
```

---

## Deployment

MXL is designed to run anywhere Linux runs, from bare-metal servers to cloud Kubernetes clusters.

### Bare Metal

**Requirements**:
- Linux kernel 4.18+ (for `CLOCK_TAI` support)
- tmpfs mount at `/dev/shm` (standard on most distros)
- User/group permissions for the MXL domain directory

**Setup**:
```bash
# Create MXL domain
mkdir -p /dev/shm/mxl
chmod 0770 /dev/shm/mxl
chown root:video /dev/shm/mxl

# Run your media function
./my-media-function
```

### Docker

**Key Considerations**:
- Mount `/dev/shm` as a volume with appropriate size (`--shm-size`)
- Share the MXL domain directory between containers using bind mounts or volumes
- Use `--ipc=shareable` and `--ipc=container:<id>` to share IPC namespace (for futex synchronization)

**Example `docker-compose.yaml`**:
```yaml
version: '3.8'
services:
  writer:
    image: my-video-source
    volumes:
      - mxl-domain:/dev/shm/mxl
    shm_size: 2gb
    ipc: shareable

  reader:
    image: my-video-sink
    volumes:
      - mxl-domain:/dev/shm/mxl
    ipc: service:writer

volumes:
  mxl-domain:
    driver: local
    driver_opts:
      type: tmpfs
      device: tmpfs
```

See [examples/README.md](examples/README.md) for full Docker Compose and Dockerfile examples.

### Kubernetes

**Pod Configuration**:
- Use `emptyDir` volumes with `medium: Memory` for the MXL domain
- Set `shareProcessNamespace: true` to enable IPC between containers in the same pod
- Configure `resources.limits.memory` to include tmpfs overhead

**Example Pod Spec**:
```yaml
apiVersion: v1
kind: Pod
metadata:
  name: mxl-pipeline
spec:
  shareProcessNamespace: true
  containers:
  - name: video-writer
    image: my-video-source
    volumeMounts:
    - name: mxl-domain
      mountPath: /dev/shm/mxl
    resources:
      limits:
        memory: 4Gi

  - name: video-reader
    image: my-video-sink
    volumeMounts:
    - name: mxl-domain
      mountPath: /dev/shm/mxl

  volumes:
  - name: mxl-domain
    emptyDir:
      medium: Memory
      sizeLimit: 2Gi
```

See [examples/kube-deployment.yaml](examples/kube-deployment.yaml) for a complete deployment manifest.

### Cloud Deployments

**AWS EKS**: Use EFA (Elastic Fabric Adapter) with the MXL Fabrics API for inter-node flows.
**GCP GKE**: Use gVNIC with the Fabrics API.
**Azure AKS**: Use accelerated networking with the Fabrics API.

All major cloud providers offer sub-millisecond time synchronization (AWS Time Sync Service, GCP NTP, Azure Time Sync), ensuring TAI timestamps remain accurate across nodes.

---

## Testing

MXL includes comprehensive unit tests, integration tests, and performance benchmarks.

### Running Tests

```bash
# Build tests
cmake -B build -DBUILD_TESTING=ON
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test suite
./build/lib/tests/mxl-flow-tests
./build/lib/tests/mxl-time-tests
```

### Test Coverage

See [lib/tests/SUMMARY.md](lib/tests/SUMMARY.md) for detailed test documentation, including:
- Flow creation and lifecycle tests
- Discrete grain read/write tests (video)
- Continuous sample read/write tests (audio)
- Ring buffer wrap-around tests
- Multi-reader synchronization tests
- Timing and index conversion tests
- Fabrics networking tests (RDMA, EFA)

### Performance Benchmarks

```bash
# Run performance benchmarks
./build/lib/tests/mxl-perf-tests

# Typical results (Intel Xeon, 2.4 GHz):
# - Grain commit latency: 1-2 microseconds
# - Reader wakeup latency: 5-10 microseconds
# - 1080p50 v210 write throughput: 3 Gbps per writer
# - Audio float32 write throughput: 100k samples/ms
```

---

## Project Structure

The repository is organized into functional layers:

```
mxl/
├── lib/                          # Core C/C++ SDK
│   ├── include/mxl/              # Public C API headers (SUMMARY.md)
│   ├── internal/                 # Internal C++ implementation
│   │   ├── include/              # Private headers (SUMMARY.md)
│   │   └── src/                  # Core logic (SUMMARY.md)
│   ├── fabrics/                  # Networking layer (SUMMARY.md)
│   │   └── ofi/                  # libfabric (RDMA/EFA) backend
│   │       └── src/              # OFI implementation (SUMMARY.md)
│   └── tests/                    # C++ tests (SUMMARY.md)
│
├── rust/                         # Rust bindings and tools
│   ├── mxl/                      # Safe Rust API (SUMMARY.md)
│   ├── mxl-sys/                  # FFI bindings (SUMMARY.md)
│   ├── gst-mxl-rs/               # GStreamer plugin (SUMMARY.md)
│   └── examples/                 # Rust usage examples (SUMMARY.md)
│
├── tools/                        # Command-line utilities (SUMMARY.md)
│   ├── mxl-info/                 # Flow inspector (SUMMARY.md)
│   ├── mxl-gst/                  # GStreamer pipelines (SUMMARY.md)
│   └── mxl-fabrics-demo/         # Networking demo (SUMMARY.md)
│
├── docs/                         # Documentation
│   ├── Architecture.md           # System design, shared memory model
│   ├── Usage.md                  # API usage patterns
│   ├── Timing.md                 # TAI timestamps, ring buffer indexing
│   ├── Addressability.md         # Flow discovery, UUID mapping
│   ├── Configuration.md          # SDK configuration options
│   ├── Building.md               # Build instructions (CMake, devcontainer)
│   └── Tools.md                  # Tool usage and GStreamer pipelines
│
├── examples/                     # Deployment examples
│   ├── docker-compose.yaml       # Docker Compose multi-container setup
│   ├── kube-deployment.yaml      # Kubernetes deployment manifest
│   └── Dockerfile.*              # Sample container images
│
├── GOVERNANCE/                   # Project governance
│   ├── GOVERNANCE.md             # Governance model
│   └── CHARTER.pdf               # Technical charter
│
├── CONTRIBUTING.md               # Contribution guidelines
├── SECURITY.md                   # Security policy
├── CODE_OF_CONDUCT.md            # Community standards
└── LICENSE.txt                   # Apache 2.0 license
```

---

## Key Characteristics

MXL is built on principles proven in hyperscale cloud infrastructure, adapted for broadcast media constraints.

- **Implemented in C++, exposed via C API**: Portable ABI, bindable to any language (Python, Rust, Go, Java).
- **Rust bindings included**: Safe, zero-cost wrappers with lifetime tracking and type safety.
- **Zero-copy by design**: `mmap()` shared memory directly. No SDK-side buffering, no hidden copies.
- **Asynchronous and non-blocking**: Readers wait via futex. Writers never block on readers. Backpressure is explicit (ring overrun errors).
- **Thin library, minimal dependencies**: Core SDK depends only on glibc (POSIX mmap, futex). Fabrics layer adds libfabric for RDMA.
- **Container-native**: Flows are tmpfs files with POSIX permissions. Enumerate flows with `ls`. Inspect flow JSON with `cat`. Delete stale flows with `rm`.
- **Format support**: v210 video (10-bit 4:2:2), v210a video with alpha, float32 audio, SMPTE ST 291 ancillary data.
- **Cloud and on-premise**: Runs on bare-metal servers, Docker, Kubernetes, AWS EKS, GCP GKE, Azure AKS.

---

## Motivation

The professional broadcast industry is shifting from hardware-centric systems to software-defined, container-based architectures. This transformation promises flexibility, scalability, and cloud-native operational models, but introduces severe interoperability challenges.

The [EBU Dynamic Media Facility (DMF) Reference Architecture](https://tech.ebu.ch/dmf/ra) addresses this by defining a **standardized media exchange layer** inspired by cloud hyperscaler designs. Media functions (ingest, processing, playout) become discrete, containerized microservices deployed on a common platform. They communicate through a high-performance data plane, enabling:

- **Asynchronous workflows**: Faster-than-live processing, decoupled production stages.
- **Flexible deployment**: On-premise, edge, or public cloud. Scale compute and bandwidth dynamically.
- **Vendor interoperability**: Open exchange format eliminates proprietary conversion layers.

**MXL is the open-source implementation of that exchange layer.**

The European Broadcasting Union (EBU) and North American Broadcasters Association (NABA) are pursuing an **implement-first strategy**: build working code with broadcasters and vendors, demonstrate real-world use cases, iterate based on deployment feedback. The first alpha was released in June 2025.

The long-term goal is to establish MXL as a **baseline for open, interoperable live production**, fostering innovation across the entire media ecosystem.

![MXL Layer Diagram](https://github.com/dmf-mxl/mxl/blob/53e889c888b2daceb4bf550943f3a194f559f182/docs/Media%20eXchange%20Layer.png "MXL Layer Diagram")

---

## Learning More

### Tutorials and Workshops

- [MXL Hands-On Workshop](https://github.com/cbcrc/mxl-hands-on): Guided exercises to set up and use MXL from scratch.

### API Documentation

Each module includes a `SUMMARY.md` file explaining its purpose, design, and usage:

- [Public C API](lib/include/mxl/SUMMARY.md): The front door to MXL. Platform layer, data formats, flow lifecycle, timing helpers.
- [Internal C++ Implementation](lib/internal/include/mxl-internal/SUMMARY.md): Shared memory management, futex synchronization, flow parsing.
- [Fabrics Networking Layer](lib/fabrics/SUMMARY.md): RDMA and EFA support for multi-host flows.
- [libfabric Backend](lib/fabrics/ofi/src/SUMMARY.md): OFI provider integration.
- [Rust Safe API](rust/mxl/src/SUMMARY.md): Safe wrappers, lifetime management, Rust idioms.
- [Rust FFI Bindings](rust/mxl-sys/src/SUMMARY.md): Raw `unsafe` bindings to the C API.
- [GStreamer Plugin](rust/gst-mxl-rs/src/SUMMARY.md): `mxlsrc` and `mxlsink` elements for GStreamer pipelines.

### Tools

- [mxl-info](tools/mxl-info/SUMMARY.md): Inspect flows, dump metadata, monitor write rates.
- [mxl-gst](tools/mxl-gst/SUMMARY.md): Pre-built GStreamer pipelines for video playback and streaming.
- [mxl-fabrics-demo](tools/mxl-fabrics-demo/SUMMARY.md): Demonstrate inter-host flow replication over RDMA.

### Community and Support

- [GitHub Issues](https://github.com/dmf-mxl/mxl/issues): Report bugs, request features.
- [GitHub Discussions](https://github.com/dmf-mxl/mxl/discussions): Ask questions, share use cases.
- [EBU DMF Working Group](https://tech.ebu.ch/groups/dmf): Join the standardization effort.

---

## License

This code is licensed under the [Apache License 2.0](./LICENSE.txt).

Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).

---

## Next Steps

1. **Clone the repository**:
   ```bash
   git clone https://github.com/dmf-mxl/mxl.git
   cd mxl
   ```

2. **Build the SDK**: Follow the instructions in [docs/Building.md](docs/Building.md).

3. **Run a test pipeline**: Use the [mxl-gst](tools/mxl-gst/SUMMARY.md) tool to create a video flow and play it back.

4. **Integrate into your application**: Include `<mxl/mxl.h>`, link against `libmxl.so`, and start exchanging media.

5. **Join the community**: Participate in [discussions](https://github.com/dmf-mxl/mxl/discussions), open issues, contribute code.

Welcome to MXL. Let's build the future of broadcast together.
