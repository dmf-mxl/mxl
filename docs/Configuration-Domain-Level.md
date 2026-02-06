<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Configuration: Domain level configuration

Domain level configuration is stored in an optional 'options.json' file at the root of the MXL domain. If present, the MXL SDK will look for specific options defined in the table below and configure itself accordingly.

| Option        | Description                | Default Value | Unit |
|----------------|---------------------------|---------------|------|
| `urn:x-mxl:option:history_duration/v1.0`         | Depth, in nanoseconds, of a ringbuffer         | 200'000'000ns   | nanoseconds |

## Example 'options.json' file

This options file will configure the depth of the ringbuffers to 500'000'000ns (500ms):

```json
{
    "urn:x-mxl:option:history_duration/v1.0": 500000000
}
```

## Domain-level configuration details

**History duration:**

The history duration determines how far back readers can look in the ring buffer. This affects:

- The number of grains allocated for discrete flows
- The buffer size for continuous flows
- Memory usage per flow

**Calculating grain count from history duration:**

For a discrete flow (e.g., video), the number of grains is:

```
grain_count = ceil(history_duration_ns / grain_duration_ns)
```

Example:
- Frame rate: 29.97 fps (30000/1001)
- Grain duration: 33,366,666 ns
- History duration: 200,000,000 ns (200ms)
- Grain count: ceil(200,000,000 / 33,366,666) = 6 grains

**For continuous flows (e.g., audio):**

The buffer length is calculated similarly:

```
buffer_length = ceil(history_duration_ns * sample_rate / 1,000,000,000)
```

Example:
- Sample rate: 48 kHz
- History duration: 200,000,000 ns (200ms)
- Buffer length: ceil(0.2 * 48000) = 9600 samples per channel

## When to adjust history duration

**Increase history duration if:**

- Readers experience high latency (e.g., processing-heavy workloads)
- Network jitter is significant (for future remote MXL scenarios)
- You need to support burst processing (read multiple grains at once)
- Multiple readers with different read rates need to coexist

**Decrease history duration if:**

- Memory is constrained
- All readers consume grains immediately (low latency pipelines)
- The domain hosts many flows and memory usage is a concern

**Trade-offs:**

- Larger history = more memory usage = longer tolerable reader latency
- Smaller history = less memory usage = readers must keep up closely with writers

[Back to Configuration overview](./Configuration.md)
