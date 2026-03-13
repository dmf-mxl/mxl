<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Multi-Stage Docker Build

Self-contained Dockerfiles that compile MXL from source and produce minimal runtime images.
No pre-built Linux binaries required — works on any machine with Docker or Podman.

## Overview

```
                    /dev/shm/mxl (shared memory)
                   ┌─────────────────────────────┐
                   │         MXL Domain           │
                   └──────┬──────────────┬────────┘
                          │              │
              writes to   │              │  reads from
                          │              │
┌─────────────────────────┴──┐   ┌───────┴──────────────────┐    SRT :5000    ┌─────┐
│         Writers            │   │        Reader             │ ─────────────→ │ VLC │
│                            │   │                           │                │     │
│  mxl-gst-testsrc           │   │  mxl-info (list flows)   │                │     │
│  mxl-gst-looping-filesrc   │   │  mxl-gst-sink (SRT out)  │                │     │
└────────────────────────────┘   └───────────────────────────┘                └─────┘
```

MXL containers communicate via **shared memory** (`/dev/shm/mxl`). Writers publish media
flows into the domain, readers discover and consume them. The reader can output video
via SRT for viewing in VLC or other media players.

## Files

| File | Description |
|------|-------------|
| **Dockerfiles** | |
| `Dockerfile.writer.multistage` | Writer: generates a test pattern (video + audio) |
| `Dockerfile.reader.multistage` | Reader: discovers flows, streams video via SRT |
| `Dockerfile.writer.video.loop.multistage` | Writer: loops a sample video transport stream ([source](https://tsduck.io/streams/uk-freeview/546000000.ts)) |
| `Dockerfile.writer.audio.loop.multistage` | Writer: loops a sample audio transport stream ([source](https://tsduck.io/streams/hotbird-13.0E/hotbird130E-ts6000-2018-05-11.ts)) |
| **Compose** | |
| `docker-compose.multistage.yaml` | Docker Compose with init container for `/dev/shm/mxl` |
| `podman-compose.yaml` | Podman Compose for macOS (no init container, SELinux workarounds) |
| **Tools** | |
| `stream.sh` | Interactive SRT stream selector (list flows, start/stop streams) |

## Quick Start (Docker)

All commands are run from the **project root** directory.

### 1. Build

```bash
# Test-source writer + reader
docker build -t mxl-writer -f examples/Dockerfile.writer.multistage .
docker build -t mxl-reader -f examples/Dockerfile.reader.multistage .

# Looping file-source writers (optional)
docker build -t mxl-writer-video-looper -f examples/Dockerfile.writer.video.loop.multistage .
docker build -t mxl-writer-audio-looper -f examples/Dockerfile.writer.audio.loop.multistage .
```

> **Note:** The first build takes ~20 minutes (compiling MXL + vcpkg dependencies).
> Subsequent builds use Docker layer caching and are much faster.

### 2. Run with Docker Compose

```bash
# Test-source writer + reader
docker compose -f examples/docker-compose.multistage.yaml --profile test-source up

# Looping file-source writers + reader
docker compose -f examples/docker-compose.multistage.yaml --profile looping-file-source up

# All writers + reader
docker compose -f examples/docker-compose.multistage.yaml --profile '*' up
```

The compose file uses **profiles** to select which writers to start.
The reader always starts (no profile). An init container creates `/dev/shm/mxl` automatically.

### 3. View the video stream

Use the provided helper script:

```bash
./examples/stream.sh
```

This will:
1. List all available MXL flows with type detection (🎬 Video / 🔊 Audio)
2. Let you select a flow by number
3. Start the SRT stream via `mxl-gst-sink`

Then open **VLC** → File → Open Network → `srt://127.0.0.1:5000?mode=caller`

To stop an active stream:

```bash
./examples/stream.sh stop
```

#### Manual streaming (without script)

```bash
# List available flows
docker exec reader-media-function /app/mxl-info -d /domain -l

# Start SRT video stream (replace <flow-uuid> with actual ID)
docker exec reader-media-function /app/mxl-gst-sink -d /domain -v <flow-uuid>
```

### 4. Stop

```bash
docker compose -f examples/docker-compose.multistage.yaml down
```

## macOS / Podman Setup

On macOS, Podman runs containers inside a Linux VM. Additional setup is required.

### One-time VM setup

```bash
# Stop VM to reconfigure
podman machine stop

# Allocate sufficient resources for real-time video processing
podman machine set --cpus 4 --memory 8192

# Start VM
podman machine start

# Create shared memory directory inside VM
podman machine ssh "mkdir -p /dev/shm/mxl"
```

### Run with Podman Compose

```bash
# Test-source writer + reader
podman-compose -f examples/podman-compose.yaml --profile test-source up

# Looping file-source writers + reader
podman-compose -f examples/podman-compose.yaml --profile looping-file-source up

# All writers + reader
podman-compose -f examples/podman-compose.yaml --profile '*' up

# Stop
podman-compose -f examples/podman-compose.yaml down
```

### Podman-specific notes

- **No init container:** podman-compose does not support `depends_on: condition: service_completed_successfully`,
  so `/dev/shm/mxl` must be created manually via `podman machine ssh`.
- **SELinux:** The compose file includes `security_opt: label=disable` to prevent permission errors.
- **Higher resource limits:** Configured for the upgraded VM (2 CPUs / 1–2 GB per container).

## Compose Profiles

Both compose files use profiles to control which writers start:

| Profile | Services started |
|---------|-----------------|
| `test-source` | `writer-media-function` + `reader-media-function` |
| `looping-file-source` | `writer-looper-video-media-function` + `writer-looper-audio-media-function` + `reader-media-function` |
| `'*'` | All services |
| *(none)* | Only `reader-media-function` (and `init` on Docker) |

## Comparison: Multistage vs. Standard Dockerfiles

| | Standard (e.g. `Dockerfile.reader.txt`) | Multistage (e.g. `Dockerfile.reader.multistage`) |
|---|---|---|
| Pre-built binaries needed | ✅ Yes (build on Linux host first) | ❌ No (builds from source in container) |
| Works on macOS | ❌ No | ✅ Yes (via Podman or Docker Desktop) |
| Build time | Fast (~seconds) | Slow (~20 min first build, cached after) |
| CI/CD ready | Needs separate build step | Fully self-contained |
| Final image size | Small | Small (build stage discarded) |

## Comparison: Docker Compose vs. Podman Compose

| | `docker-compose.multistage.yaml` | `podman-compose.yaml` |
|---|---|---|
| Init container | ✅ Auto-creates `/dev/shm/mxl` | ❌ Manual (`podman machine ssh`) |
| SELinux workaround | Not needed | `security_opt: label=disable` |
| Resource limits | Original values (lower) | Higher (VM overhead) |
| Platform | Linux, Docker Desktop | macOS Podman |
