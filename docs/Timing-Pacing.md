<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Timing: Pacing a write loop

Writers should pace their writes to avoid overrunning the ring buffer. Here's how to calculate sleep times.

## Video grain pacing

```c
#include <time.h>

// Calculate frame period
mxlRational frame_rate = {30000, 1001};  // 29.97 fps
uint64_t frame_period_ns = mxlRationalToNanoseconds(1, frame_rate);

while (running) {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Write grain
    write_video_grain(instance, writer);

    // Calculate elapsed time
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL +
                          (end.tv_nsec - start.tv_nsec);

    // Sleep for remaining time
    if (elapsed_ns < frame_period_ns) {
        uint64_t sleep_ns = frame_period_ns - elapsed_ns;
        struct timespec sleep_time = {
            .tv_sec = sleep_ns / 1000000000ULL,
            .tv_nsec = sleep_ns % 1000000000ULL
        };
        nanosleep(&sleep_time, NULL);
    }
}
```

## Audio sample pacing

```c
// Calculate batch period
uint32_t sample_rate = 48000;
uint32_t batch_size = 512;
uint64_t batch_period_ns = (batch_size * 1000000000ULL) / sample_rate;
// Result: 10666666 ns (10.67ms for 512 samples at 48kHz)

while (running) {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Write audio batch
    write_audio_samples(instance, writer, batch_size);

    // Calculate elapsed time
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL +
                          (end.tv_nsec - start.tv_nsec);

    // Sleep for remaining time
    if (elapsed_ns < batch_period_ns) {
        uint64_t sleep_ns = batch_period_ns - elapsed_ns;
        struct timespec sleep_time = {
            .tv_sec = sleep_ns / 1000000000ULL,
            .tv_nsec = sleep_ns % 1000000000ULL
        };
        nanosleep(&sleep_time, NULL);
    }
}
```

## Handling timing drift

For long-running processes, use absolute timestamps to avoid drift:

```c
// Initialize start time
struct timespec tai_start;
clock_gettime(CLOCK_TAI, &tai_start);
uint64_t start_time_ns = (uint64_t)tai_start.tv_sec * 1000000000ULL + tai_start.tv_nsec;

uint64_t frame_period_ns = mxlRationalToNanoseconds(1, frame_rate);
uint64_t frame_count = 0;

while (running) {
    // Calculate expected timestamp for this frame
    uint64_t expected_timestamp = start_time_ns + (frame_count * frame_period_ns);

    // Get current time
    struct timespec now;
    clock_gettime(CLOCK_TAI, &now);
    uint64_t current_time_ns = (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;

    // Sleep until expected time
    if (current_time_ns < expected_timestamp) {
        uint64_t sleep_ns = expected_timestamp - current_time_ns;
        struct timespec sleep_time = {
            .tv_sec = sleep_ns / 1000000000ULL,
            .tv_nsec = sleep_ns % 1000000000ULL
        };
        nanosleep(&sleep_time, NULL);
    }

    // Write grain with expected timestamp
    uint64_t grain_index = expected_timestamp / frame_period_ns;
    mxlGrainInfo grain_info;
    uint8_t* buffer;
    mxlFlowWriterOpenGrain(instance, writer, grain_index, &grain_info, &buffer);
    // ... fill buffer ...
    grain_info.originTimestamp = expected_timestamp;
    mxlFlowWriterCommitGrain(instance, writer, &grain_info);

    frame_count++;
}
```

This approach prevents cumulative timing errors from nanosleep inaccuracies.

[Back to Timing overview](./Timing.md)
