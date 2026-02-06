<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Timing: Time functions

All time helpers operate on nanoseconds and rational time bases. Continuous flows typically pass the sample rate (`grainRate`) directly to these
helpers when scheduling wake-ups or calculating how far to advance `headIndex`. Refer to `mxl/time.h` for the canonical API surface.

## Key time API functions

```c
// Convert rational rate to nanoseconds per unit
// Example: mxlRationalToNanoseconds(1, {30000, 1001}) returns 33366666 (frame period)
uint64_t mxlRationalToNanoseconds(uint64_t count, mxlRational rate);

// Convert nanoseconds to count based on rate
// Example: mxlNanosecondsToRational(1000000000, {48000, 1}) returns 48000 (samples in 1 second)
uint64_t mxlNanosecondsToRational(uint64_t nanoseconds, mxlRational rate);

// Get current TAI time in nanoseconds since SMPTE 2059 epoch
uint64_t mxlGetTaiTimestampNs(void);

// Add/subtract rational values
mxlRational mxlRationalAdd(mxlRational a, mxlRational b);
mxlRational mxlRationalSubtract(mxlRational a, mxlRational b);

// Compare rational values
int mxlRationalCompare(mxlRational a, mxlRational b);  // -1, 0, or 1
```

## Example: Calculate samples per video frame

```c
// How many audio samples correspond to one video frame?
mxlRational video_rate = {30000, 1001};  // 29.97 fps
mxlRational audio_rate = {48000, 1};     // 48kHz

uint64_t frame_period_ns = mxlRationalToNanoseconds(1, video_rate);
uint64_t samples_per_frame = mxlNanosecondsToRational(frame_period_ns, audio_rate);
// Result: 1601 samples (approximately)

// Note: This will vary slightly frame-to-frame due to rounding
// Use origin timestamps for exact alignment
```

[Back to Timing overview](./Timing.md)
