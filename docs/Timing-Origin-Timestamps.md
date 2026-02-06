<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Timing: Origin timestamps (OTS)

MXL does not try to hide or compensate for jitter and latency automatically: it relies instead on the origin timestamps of the media grains for accurate indexing. If origin timestamps are not available explicitly through RTP header extensions they can be inferred at capture time by 'unrolling' the RTP timestamps of 2110 and AES67 packets or through other transport-specific mechanisms.

In absence of origin timestamps a receiver media function may need to re-timestamp incoming media and write at $grainIndex(T{now})$.

MXL Flow replication between hosts will preserve the indexing of the grains. In other words, the flow IDs, grain indexes and ring buffer size and indexes.

## How origin timestamps work in practice

Origin timestamps tell readers WHEN the grain was captured or generated, not when it was written to MXL. This allows:

1. **Preserving timing across multiple hops**: A grain captured at timestamp T and written to MXL at T+50ms retains T as its origin timestamp. Readers can reconstruct the original timing.

2. **Jitter handling**: If a writer experiences scheduling jitter and writes grains at irregular intervals, origin timestamps allow readers to play back at the correct rate.

3. **Multi-flow synchronization**: Video and audio flows with origin timestamps can be aligned even if they experience different processing delays.

**Example: SMPTE ST 2110 receiver**

```c
// RTP packet arrives with RTP timestamp
uint32_t rtp_timestamp = packet->rtp_header.timestamp;
uint32_t rtp_clock_rate = 90000;  // 90kHz for video

// Unroll RTP timestamp to full 64-bit timestamp
// (requires tracking RTP timestamp wraps)
uint64_t unrolled_rtp_timestamp = unroll_rtp_timestamp(rtp_timestamp);

// Convert RTP timestamp to TAI nanoseconds
// (requires knowing the mapping between RTP and TAI time)
uint64_t origin_timestamp_ns = rtp_to_tai_ns(unrolled_rtp_timestamp, rtp_clock_rate);

// Write grain with origin timestamp
mxlGrainInfo grain_info;
grain_info.originTimestamp = origin_timestamp_ns;
mxlFlowWriterCommitGrain(instance, writer, &grain_info);
```

**Example: Test pattern generator**

```c
// Generator produces frames in real-time
// Use current TAI time as origin timestamp
struct timespec ts;
clock_gettime(CLOCK_TAI, &ts);
uint64_t origin_timestamp = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

// Calculate grain index from timestamp
uint64_t grain_duration_ns = mxlRationalToNanoseconds(1, grain_rate);
uint64_t grain_index = origin_timestamp / grain_duration_ns;

// Write grain
mxlFlowWriterOpenGrain(instance, writer, grain_index, &grain_info, &buffer);
// ... fill buffer ...
grain_info.originTimestamp = origin_timestamp;
mxlFlowWriterCommitGrain(instance, writer, &grain_info);
```

## Example Flow Writer behaviors

- A 2110-20 receiver media function can unroll the RTP timestamp of the first RTP packet of the frame and use it to compute the grain index write to. This index may be slightly in the past.
- A media function that generates grains locally (CG, Test Pattern Generator, etc) can simply use the current TAI time as a timestamp and convert it to a grain index.

## Example Flow Readers behaviors

A media function consuming one or many flows may use multiple alignment strategies.

- If inter-flow alignment is not required it can simply consume the flows at their respective 'head' position.
- If alignment is required then a reading strategy could be to start reading at the lower grain index :

$$
ReadIndex = min(F1_{head} ... FN_{head})
$$

[Back to Timing overview](./Timing.md)
