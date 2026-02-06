<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Synchronization Groups

Synchronization groups allow you to align multiple flows (e.g., video + audio) to the same timestamps.

## Complete example

```c
#include <mxl/mxl.h>
#include <mxl/flow.h>
#include <mxl/sync.h>

int read_synchronized_av(mxlInstance instance,
                         mxlFlowReader video_reader,
                         mxlFlowReader audio_reader) {
    // Create synchronization group
    mxlSyncGroup sync_group;
    mxlStatus status = mxlCreateSyncGroup(instance, &sync_group);
    if (status != MXL_STATUS_OK) {
        fprintf(stderr, "Failed to create sync group\n");
        return -1;
    }

    // Add flows to the group
    status = mxlSyncGroupAddFlow(instance, sync_group, video_reader);
    if (status != MXL_STATUS_OK) {
        fprintf(stderr, "Failed to add video flow to sync group\n");
        return -1;
    }

    status = mxlSyncGroupAddFlow(instance, sync_group, audio_reader);
    if (status != MXL_STATUS_OK) {
        fprintf(stderr, "Failed to add audio flow to sync group\n");
        return -1;
    }

    // Read synchronized data
    while (true) {
        // Wait for all flows to have data at approximately the same timestamp
        uint64_t sync_timestamp_ns;
        uint64_t timeout_ns = 100000000;  // 100ms

        status = mxlSyncGroupWait(instance, sync_group, timeout_ns, &sync_timestamp_ns);
        if (status == MXL_STATUS_TIMEOUT) {
            printf("Timeout waiting for sync\n");
            continue;
        } else if (status != MXL_STATUS_OK) {
            fprintf(stderr, "Sync group wait failed: %d\n", status);
            break;
        }

        printf("Synchronized at timestamp %llu ns\n", sync_timestamp_ns);

        // Read video grain at this timestamp
        mxlGrainInfo video_grain;
        const uint8_t* video_buffer;
        uint64_t video_index = /* convert sync_timestamp_ns to grain index */;

        status = mxlFlowReaderGetGrain(instance, video_reader, video_index, 0,
                                       &video_grain, &video_buffer);
        if (status == MXL_STATUS_OK) {
            process_video_grain(video_buffer, video_grain.grainSize);
        }

        // Read audio samples at this timestamp
        // (audio uses sample index instead of grain index)
        mxlFlowInfo audio_info;
        mxlFlowReaderGetInfo(instance, audio_reader, &audio_info);
        uint32_t sample_rate = audio_info.config.common.grainRate.numerator;
        uint64_t audio_sample_index = (sync_timestamp_ns * sample_rate) / 1000000000ULL;

        mxlWrappedMultiBufferSlice audio_slices;
        status = mxlFlowReaderGetSamples(instance, audio_reader, audio_sample_index, 512, 0, &audio_slices);
        if (status == MXL_STATUS_OK) {
            process_audio_samples(&audio_slices);
        }
    }

    // Cleanup
    mxlDestroySyncGroup(instance, sync_group);
    return 0;
}
```

## Key points

Synchronization groups automatically align flows based on their origin timestamps, handling differences in frame rates and sample rates.

---

Back to [Usage overview](./Usage.md)
