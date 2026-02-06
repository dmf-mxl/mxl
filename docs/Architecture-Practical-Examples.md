<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture: Practical Examples

This section collects concrete, runnable examples and deployment recipes. The architecture sections describe _how_ things work; the examples here show _how to use them_.

## Security & permissions setup

**Read-only consumer**: A monitoring application runs as user `monitor` in group `video`. Set the MXL domain to `root:video` with `0750` permissions. The monitor can read flows but cannot create or modify them.

**Multi-tenant isolation**: Two different media functions run as different users (`encoder1` and `encoder2`). Each creates flows owned by their respective user. A shared consumer runs as a service account that is granted group-read access to both users' flows.

**Production safeguards**: Mount the MXL domain read-only for readers in production. Writers operate on a read-write mount. This physically prevents accidental writes from reader processes.

```bash
# Domain directory
mkdir -p /dev/shm/mxl
chown mxl-writer:mxl-readers /dev/shm/mxl
chmod 2775 /dev/shm/mxl  # setgid ensures new flows inherit group

# Individual flow (created by writer)
# /dev/shm/mxl/550e8400-e29b-41d4-a716-446655440000.mxl-flow/
# Owner: mxl-writer, Group: mxl-readers, Perms: 2750
```

---

## Container deployment: Docker / Podman

**Single-host, multiple containers:**

```bash
# Create shared tmpfs volume
docker volume create --driver local \
    --opt type=tmpfs \
    --opt device=tmpfs \
    --opt o=size=2g,uid=1000,gid=1000 \
    mxl-domain

# Writer container
docker run -v mxl-domain:/mxl:rw writer-app

# Reader container
docker run -v mxl-domain:/mxl:ro reader-app
```

**Bind mount from host tmpfs:**

```bash
# Writer
docker run -v /dev/shm/mxl:/mxl:rw writer-app

# Reader (read-only)
docker run -v /dev/shm/mxl:/mxl:ro reader-app
```

---

## Container deployment: Kubernetes

Use a shared emptyDir volume with medium: Memory:

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: mxl-pipeline
spec:
  volumes:
  - name: mxl-domain
    emptyDir:
      medium: Memory
      sizeLimit: 2Gi
  containers:
  - name: writer
    image: writer-app
    volumeMounts:
    - name: mxl-domain
      mountPath: /mxl
  - name: reader
    image: reader-app
    volumeMounts:
    - name: mxl-domain
      mountPath: /mxl
      readOnly: true
```

For multi-pod scenarios, use a hostPath volume:

```yaml
volumes:
- name: mxl-domain
  hostPath:
    path: /dev/shm/mxl
    type: DirectoryOrCreate
```

**Important considerations:**

- Use resource limits to prevent memory exhaustion.
- The MXL domain size should accommodate the ring buffer history plus overhead.
- Monitor domain usage with `df` or `du` commands.
- Use `mxlGarbageCollectFlows()` in orchestration to clean up stale flows.

---

## Partial Grain I/O (sliced writes)

The following example demonstrates writing and reading a grain in 8 slices using multiple `mxlFlowWriterCommitGrain()` calls. (From the [unit tests](../lib/tests/test_flows.cpp).)

```c++
    ///
    /// Write and read a grain in 8 slices
    ///

    /// Open the grain.
    mxlGrainInfo gInfo;
    uint8_t *buffer = nullptr;

    /// Open the grain for writing.
    REQUIRE( mxlFlowWriterOpenGrain( instanceWriter, writer, index, &gInfo, &buffer ) == MXL_STATUS_OK );

    const size_t maxSlice = 8;
    auto sliceSize = gInfo.grainSize / maxSlice;
    for ( size_t slice = 0; slice < maxSlice; slice++ )
    {
        /// Write a slice the grain.
        gInfo.committedSize += sliceSize;
        REQUIRE( mxlFlowWriterCommitGrain( instanceWriter, writer, &gInfo ) == MXL_STATUS_OK );

        mxlFlowRuntimeInfo sliceFlowRuntimeInfo;
        REQUIRE( mxlFlowReaderGetRuntimeInfo( instanceReader, reader, &sliceFlowRuntimeInfo ) == MXL_STATUS_OK );
        REQUIRE( sliceFlowRuntimeInfo.headIndex == index );

        /// Read back the partial grain using the flow reader.
        REQUIRE( mxlFlowReaderGetGrain( instanceReader, reader, index, 8, &gInfo, &buffer ) == MXL_STATUS_OK );

        // Validate the committed size
        REQUIRE( gInfo.committedSize == sliceSize * ( slice + 1 ) );
    }
```

---

## Reading continuous samples

Reading a window of samples requires three pieces of information:

1. The desired absolute sample index (`index`). This is the last sample you want to include.
2. The number of samples to look backwards by (`count`). This must not exceed `bufferLength / 2`.
3. The timeout you are willing to wait for the window (`timeoutNs`).

```c
// Retrieve flow configuration first
mxlFlowInfo info = {};
MXL_THROW_IF_FAILED(mxlFlowReaderGetInfo(reader, &info));

const mxlContinuousFlowConfigInfo *continuous = &info.config.continuous;
const uint32_t channelCount = continuous->channelCount;
const uint32_t channelBufferLength = continuous->bufferLength;
const mxlRational sampleRate = info.config.common.grainRate;
// sampleRate == samples per second for continuous flows
```

```c
// Read the latest 256 samples per channel, waiting up to 5 ms.
mxlWrappedMultiBufferSlice slices;
mxlFlowRuntimeInfo runtime;
mxlFlowReaderGetRuntimeInfo(reader, &runtime);

const uint64_t lastSample = runtime.headIndex;
const size_t windowLength = 256;
mxlFlowReaderGetSamples(
    reader,
    lastSample,
    windowLength,
    5'000'000,  // 5 milliseconds
    &slices);

const size_t channels = slices.count;
const size_t strideBytes = slices.stride;
const size_t fragment0Samples = slices.base.fragments[0].size / sizeof(float);
const size_t fragment1Samples = slices.base.fragments[1].size / sizeof(float);

for (size_t channel = 0; channel < channels; ++channel) {
    const uint8_t *channelBase0 = static_cast<const uint8_t *>(slices.base.fragments[0].pointer) + channel * strideBytes;
    const uint8_t *channelBase1 = static_cast<const uint8_t *>(slices.base.fragments[1].pointer) + channel * strideBytes;

    const float *firstSlice = reinterpret_cast<const float *>(channelBase0);
    const float *secondSlice = reinterpret_cast<const float *>(channelBase1);

    // Process the first fragment (may already contain the entire window).
    for (size_t i = 0; i < fragment0Samples; ++i) {
        consume_sample(channel, firstSlice[i]);
    }

    // Only needed when the window wrapped around the ring buffer.
    for (size_t i = 0; i < fragment1Samples; ++i) {
        consume_sample(channel, secondSlice[i]);
    }
}
```

---

## Writing continuous samples

Writing samples mirrors the reading pattern but uses `mxlMutableWrappedMultiBufferSlice`:

```c
mxlMutableWrappedMultiBufferSlice scratch;
mxlFlowWriterOpenSamples(writer, nextSampleIndex, batchSize, &scratch);
// Fill scratch for every channel respecting fragments and stride, exactly like the reader example.
// ...
mxlFlowWriterCommitSamples(writer);
```

---

## Additional resources

For detailed API documentation and code structure:

- **Public API**: See `lib/include/mxl/SUMMARY.md` for C API reference and `rust/mxl-rs/README.md` for Rust bindings.
- **Internal details**: See `lib/internal/SUMMARY.md` for implementation architecture and internal data structures.
- **Timing APIs**: Refer to `lib/include/mxl/time.h` for time conversion helpers and `lib/internal/Timing.h` for internal timing logic.

---

[Back to Architecture overview](./Architecture.md)
