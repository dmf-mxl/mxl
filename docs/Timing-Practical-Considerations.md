<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Timing: Practical considerations

## Clock selection

Always use `CLOCK_TAI` for MXL timestamps, not `CLOCK_REALTIME`:

```c
struct timespec ts;
clock_gettime(CLOCK_TAI, &ts);  // Correct: TAI does not have leap seconds
// clock_gettime(CLOCK_REALTIME, &ts);  // Wrong: UTC has leap seconds
```

TAI (International Atomic Time) never has leap seconds, ensuring timestamps always increase monotonically. CLOCK_REALTIME (UTC) can jump backwards when a leap second is removed.

## Handling late arrivals

If your writer falls behind schedule, decide whether to catch up or drop frames:

```c
// Option 1: Catch up (write all missed frames)
while (current_grain_index < expected_grain_index) {
    write_grain(current_grain_index);
    current_grain_index++;
}

// Option 2: Skip ahead (drop missed frames)
if (current_grain_index < expected_grain_index) {
    fprintf(stderr, "Warning: Dropped %llu frames\n",
            expected_grain_index - current_grain_index);
    current_grain_index = expected_grain_index;
}
```

## Ring buffer overrun

If the ring buffer is too small, writers can overwrite grains that readers haven't processed yet. Choose a history duration that accommodates:

- Maximum expected reader latency
- Network jitter (for remote readers)
- Burst processing delays

Rule of thumb: Use at least 200ms of history for real-time applications, more for file-based processing.

## Additional resources

- **Time API reference**: See `lib/include/mxl/time.h` for complete time function documentation
- **Internal timing implementation**: See `lib/internal/Timing.h` for timing internals
- **Synchronization API**: See `lib/include/mxl/sync.h` for multi-flow synchronization
- **Architecture**: See [Architecture.md](./Architecture.md) for ring buffer geometry details

[Back to Timing overview](./Timing.md)
