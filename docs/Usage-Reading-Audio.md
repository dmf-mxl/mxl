<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Reading Audio Samples

Reading continuous flows is similar to writing, but in reverse.

## Complete example

```c
#include <mxl/mxl.h>
#include <mxl/flow.h>

int read_audio_samples(mxlInstance instance, mxlFlowReader reader) {
    // Get flow info
    mxlFlowInfo flowInfo;
    mxlStatus status = mxlFlowReaderGetInfo(instance, reader, &flowInfo);
    if (status != MXL_STATUS_OK) return -1;

    const uint32_t channel_count = flowInfo.config.continuous.channelCount;
    const uint32_t buffer_length = flowInfo.config.continuous.bufferLength;
    const uint32_t max_window = buffer_length / 2;

    printf("Audio flow: %u channels, buffer length: %u samples\n",
           channel_count, buffer_length);

    // Read samples in a loop
    const uint32_t window_size = 256;  // Read 256 samples at a time

    while (true) {
        // Get current head index
        mxlFlowRuntimeInfo runtimeInfo;
        status = mxlFlowReaderGetRuntimeInfo(instance, reader, &runtimeInfo);
        if (status != MXL_STATUS_OK) continue;

        uint64_t head_index = runtimeInfo.headIndex;

        // Read the latest window_size samples
        mxlWrappedMultiBufferSlice slices;
        uint64_t timeout_ns = 10000000;  // 10ms

        status = mxlFlowReaderGetSamples(
            instance, reader,
            head_index,          // Read up to this sample (inclusive)
            window_size,         // Number of samples to read backwards
            timeout_ns,
            &slices
        );

        if (status == MXL_STATUS_TIMEOUT) {
            printf("Timeout waiting for samples\n");
            continue;
        } else if (status != MXL_STATUS_OK) {
            fprintf(stderr, "Failed to read samples: %d\n", status);
            return -1;
        }

        // Process each channel
        for (uint32_t ch = 0; ch < channel_count; ch++) {
            const uint8_t* frag0_base = (const uint8_t*)slices.base.fragments[0].pointer + ch * slices.stride;
            const uint8_t* frag1_base = (const uint8_t*)slices.base.fragments[1].pointer + ch * slices.stride;

            const float* frag0 = (const float*)frag0_base;
            const float* frag1 = (const float*)frag1_base;

            size_t frag0_samples = slices.base.fragments[0].size / sizeof(float);
            size_t frag1_samples = slices.base.fragments[1].size / sizeof(float);

            // Process fragment 0
            for (size_t i = 0; i < frag0_samples; i++) {
                float sample = frag0[i];
                // Do something with the sample
                process_audio_sample(ch, sample);
            }

            // Process fragment 1 (if wrapped)
            for (size_t i = 0; i < frag1_samples; i++) {
                float sample = frag1[i];
                process_audio_sample(ch, sample);
            }
        }

        printf("Processed samples up to index %llu\n", head_index);

        // Sleep to pace reads
        usleep(5000);  // 5ms
    }

    return 0;
}
```

## Key points

- Use `mxlFlowReaderGetSamples` to read a window of samples.
- The index parameter is inclusive: `GetSamples(1000, 256)` returns samples 745-1000.
- Always handle two fragments to support ring buffer wrap-around.
- Readers can only access up to `bufferLength / 2` samples in one call.

---

Back to [Usage overview](./Usage.md)
