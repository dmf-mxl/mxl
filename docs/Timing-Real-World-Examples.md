<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Timing: Real-world timing examples

## Common video frame rates

Here are grain duration calculations for common broadcast frame rates:

| Frame Rate | Numerator | Denominator | Grain Duration (ns) | Grain Duration (ms) | Grains per second |
|------------|-----------|-------------|---------------------|---------------------|-------------------|
| 23.98      | 24000     | 1001        | 41,708,333.33       | 41.71               | 23.976            |
| 24         | 24        | 1           | 41,666,666.67       | 41.67               | 24.000            |
| 25         | 25        | 1           | 40,000,000.00       | 40.00               | 25.000            |
| 29.97      | 30000     | 1001        | 33,366,666.67       | 33.37               | 29.970            |
| 30         | 30        | 1           | 33,333,333.33       | 33.33               | 30.000            |
| 50         | 50        | 1           | 20,000,000.00       | 20.00               | 50.000            |
| 59.94      | 60000     | 1001        | 16,683,333.33       | 16.68               | 59.940            |
| 60         | 60        | 1           | 16,666,666.67       | 16.67               | 60.000            |

**Code example:**

```c
#include <mxl/time.h>

// Get grain duration for 29.97 fps (30000/1001)
mxlRational frame_rate = {30000, 1001};
uint64_t grain_duration_ns = mxlRationalToNanoseconds(1, frame_rate);
// Result: 33366666 ns (approximately 33.37 ms)

// Convert timestamp to grain index
uint64_t timestamp_ns = 1000000000ULL;  // 1 second since epoch
uint64_t grain_index = timestamp_ns / grain_duration_ns;
// Result: 29 (approximately 30 frames in one second)
```

## Audio timing examples

Audio flows use sample rates instead of frame rates. The same timing math applies, but at the sample level.

| Sample Rate | Numerator | Denominator | Sample Period (ns) | Samples per ms |
|-------------|-----------|-------------|-------------------|----------------|
| 48 kHz      | 48000     | 1           | 20,833.33         | 48             |
| 96 kHz      | 96000     | 1           | 10,416.67         | 96             |

**Code example:**

```c
// Audio flow at 48kHz
mxlRational sample_rate = {48000, 1};
uint64_t sample_period_ns = mxlRationalToNanoseconds(1, sample_rate);
// Result: 20833 ns per sample

// How many samples in 10ms?
uint64_t duration_ns = 10000000;  // 10ms
uint64_t sample_count = duration_ns / sample_period_ns;
// Result: 480 samples
```

## Calculating ring buffer size

Given a desired history duration (e.g., 200ms), calculate how many grains are needed:

```c
// Example: 29.97 fps video, 200ms history
mxlRational grain_rate = {30000, 1001};
uint64_t history_duration_ns = 200000000;  // 200ms
uint64_t grain_duration_ns = mxlRationalToNanoseconds(1, grain_rate);

uint64_t grain_count = (history_duration_ns + grain_duration_ns - 1) / grain_duration_ns;
// Result: 6 grains (ceil(200ms / 33.37ms))
```

[Back to Timing overview](./Timing.md)
