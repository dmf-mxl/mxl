<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Audio Sample Writing (float32 multi-channel)

Continuous flows use a different API from discrete flows. Instead of grains, you write samples.

## Complete example

```c
#include <mxl/mxl.h>
#include <mxl/flow.h>
#include <mxl/time.h>
#include <math.h>

int write_audio_samples(mxlInstance instance, mxlFlowWriter writer,
                        uint32_t channel_count, uint32_t sample_rate) {
    // Get flow info
    mxlFlowInfo flowInfo;
    mxlStatus status = mxlFlowWriterGetInfo(instance, writer, &flowInfo);
    if (status != MXL_STATUS_OK) return -1;

    // Verify it's a continuous flow
    if (flowInfo.config.common.format != MXL_FORMAT_AUDIO) {
        fprintf(stderr, "Not an audio flow\n");
        return -1;
    }

    const uint32_t buffer_length = flowInfo.config.continuous.bufferLength;
    const uint32_t batch_size = 512;  // Write 512 samples at a time

    // Generate and write audio samples in a loop
    uint64_t sample_index = 0;

    for (int batch = 0; batch < 100; batch++) {  // Write 100 batches
        // Get current time for pacing
        struct timespec ts;
        clock_gettime(CLOCK_TAI, &ts);
        uint64_t current_time_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

        // Calculate expected sample index based on time
        // sample_index = current_time_ns / (1e9 / sample_rate)
        uint64_t expected_index = (current_time_ns * sample_rate) / 1000000000ULL;

        // Open sample buffer
        mxlMutableWrappedMultiBufferSlice slices;
        status = mxlFlowWriterOpenSamples(instance, writer, expected_index, batch_size, &slices);
        if (status != MXL_STATUS_OK) {
            fprintf(stderr, "Failed to open samples: %d\n", status);
            return -1;
        }

        // Fill each channel
        for (uint32_t ch = 0; ch < channel_count; ch++) {
            // Get pointers to the two fragments (in case of wrap-around)
            uint8_t* frag0_base = (uint8_t*)slices.base.fragments[0].pointer + ch * slices.stride;
            uint8_t* frag1_base = (uint8_t*)slices.base.fragments[1].pointer + ch * slices.stride;

            float* frag0 = (float*)frag0_base;
            float* frag1 = (float*)frag1_base;

            size_t frag0_samples = slices.base.fragments[0].size / sizeof(float);
            size_t frag1_samples = slices.base.fragments[1].size / sizeof(float);

            // Generate sine wave (440 Hz for channel 0, 880 Hz for channel 1, etc.)
            float frequency = 440.0f * (1 << ch);

            // Fill fragment 0
            for (size_t i = 0; i < frag0_samples; i++) {
                float t = (float)(expected_index - batch_size + i) / sample_rate;
                frag0[i] = 0.5f * sinf(2.0f * M_PI * frequency * t);
            }

            // Fill fragment 1 (only if wrapped around)
            for (size_t i = 0; i < frag1_samples; i++) {
                float t = (float)(expected_index - batch_size + frag0_samples + i) / sample_rate;
                frag1[i] = 0.5f * sinf(2.0f * M_PI * frequency * t);
            }
        }

        // Commit the samples
        status = mxlFlowWriterCommitSamples(instance, writer);
        if (status != MXL_STATUS_OK) {
            fprintf(stderr, "Failed to commit samples: %d\n", status);
            return -1;
        }

        printf("Wrote batch %d, samples %llu-%llu\n", batch, expected_index - batch_size, expected_index);

        // Sleep to pace the writes (simulate real-time)
        struct timespec sleep_time;
        sleep_time.tv_sec = 0;
        sleep_time.tv_nsec = (batch_size * 1000000000ULL) / sample_rate;
        nanosleep(&sleep_time, NULL);
    }

    return 0;
}
```

## Key points

- Audio flows are continuous, not discrete. Use `OpenSamples/CommitSamples` instead of `OpenGrain/CommitGrain`.
- Channels are de-interleaved. Use `stride` to navigate between channels.
- Always handle two fragments. If the window doesn't wrap around, fragment 1 will have size 0.
- Pace your writes to avoid overrunning readers. The example uses `nanosleep` for simple pacing.

---

Back to [Usage overview](./Usage.md)
