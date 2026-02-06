<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Timing: Audio/Video synchronization

Synchronizing audio and video flows requires converting between grain indices (video) and sample indices (audio) using timestamps.

## Strategy 1: Video-driven sync

Read video frames at the head, then calculate which audio samples correspond to each frame.

```c
// Read video grain
mxlFlowRuntimeInfo video_runtime;
mxlFlowReaderGetRuntimeInfo(instance, video_reader, &video_runtime);
uint64_t video_grain_index = video_runtime.headIndex;

mxlGrainInfo video_grain;
const uint8_t* video_buffer;
mxlFlowReaderGetGrain(instance, video_reader, video_grain_index, timeout_ns,
                      &video_grain, &video_buffer);

// Calculate audio sample index from video origin timestamp
mxlFlowInfo audio_info;
mxlFlowReaderGetInfo(instance, audio_reader, &audio_info);
uint32_t audio_sample_rate = audio_info.config.common.grainRate.numerator;

uint64_t audio_sample_index = (video_grain.originTimestamp * audio_sample_rate) / 1000000000ULL;

// Read corresponding audio samples
uint32_t samples_per_frame = audio_sample_rate * video_grain_duration_ns / 1000000000ULL;
mxlWrappedMultiBufferSlice audio_slices;
mxlFlowReaderGetSamples(instance, audio_reader, audio_sample_index, samples_per_frame,
                        timeout_ns, &audio_slices);

// video_grain and audio_slices are now synchronized
```

## Strategy 2: Synchronization groups

Use MXL's built-in synchronization group API to automatically align multiple flows.

```c
#include <mxl/sync.h>

// Create sync group
mxlSyncGroup sync_group;
mxlCreateSyncGroup(instance, &sync_group);

// Add flows
mxlSyncGroupAddFlow(instance, sync_group, video_reader);
mxlSyncGroupAddFlow(instance, sync_group, audio_reader);

// Wait for synchronized timestamp
uint64_t sync_timestamp_ns;
mxlSyncGroupWait(instance, sync_group, timeout_ns, &sync_timestamp_ns);

// Read grains/samples at the synchronized timestamp
// (convert sync_timestamp_ns to grain/sample index for each flow)
```

[Back to Timing overview](./Timing.md)
