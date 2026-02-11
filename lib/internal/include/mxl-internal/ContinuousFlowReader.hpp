// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ContinuousFlowReader.hpp
 * @brief FlowReader specialization for continuous flows (AUDIO samples)
 *
 * Provides the Samples API for reading sample buffers from AUDIO flows.
 * Samples are accessed by absolute index across per-channel ring buffers.
 *
 * KEY CONCEPTS:
 *
 * Sample indexing:
 * - Each sample has an absolute index (0, 1, 2, 3, ...)
 * - Ring buffer position = sampleIndex % bufferLength
 * - Example: 48000-sample buffer, requesting index 50000 returns position 50000%48000=2000
 * - All channels synchronized (same index applies to all channels)
 *
 * Tail index:
 * - Oldest available sample in ring buffer
 * - If writer is at sample 1000000 with 48000-sample buffer, tail = 1000000-48000+1
 * - Requesting sample < tail returns MXL_ERR_OUT_OF_RANGE_TOO_LATE (overwritten)
 * - Requesting sample > head returns MXL_ERR_OUT_OF_RANGE_TOO_EARLY (not yet written)
 *
 * Sample ranges:
 * - getSamples() returns a range [index, index+count)
 * - Range may wrap around ring buffer (mxlWrappedMultiBufferSlice handles this)
 * - Each channel's samples are contiguous in memory (not interleaved)
 *
 * Wrapped buffer slices:
 * - mxlWrappedMultiBufferSlice: Up to 2 slices per channel (for wrap-around)
 * - Example: Request 1000 samples starting at position 47500 in 48000-sample buffer
 *   - Slice 0: samples 47500-47999 (500 samples)
 *   - Slice 1: samples 0-499 (500 samples, wrapping)
 * - Application must process both slices or copy into linear buffer
 *
 * Zero-copy access:
 * - Pointers in mxlWrappedMultiBufferSlice point directly into mmap'd "channels" file
 * - No memcpy; payload valid until next getSamples() call or reader destruction
 * - Application must not modify payload (PROT_READ mapping)
 *
 * Lifetime guarantees:
 * - No guarantee how long samples remain valid (writer may overwrite)
 * - Application should process samples quickly or copy if needed
 * - Typically safe for bufferLength/2 samples (half the ring buffer)
 *
 * TYPICAL USAGE:
 *
 * ```cpp
 * auto* reader = instance->getFlowReader(flowId);
 * auto continuousReader = dynamic_cast<ContinuousFlowReader*>(reader);
 *
 * uint64_t sampleIndex = 0;
 * constexpr size_t samplesToRead = 1920;  // One video frame worth @ 48kHz/24fps
 *
 * while (running) {
 *     mxlWrappedMultiBufferSlice slices;
 *     auto deadline = currentTime(Clock::Realtime) + fromMilliSeconds(100);
 *
 *     // Wait for next batch of samples
 *     auto status = continuousReader->getSamples(sampleIndex, samplesToRead, deadline, slices);
 *     if (status == MXL_STATUS_SUCCESS) {
 *         for (size_t ch = 0; ch < slices.channelCount; ch++) {
 *             // Process slice 0 (always present)
 *             processSamples(ch, (float*)slices.channels[ch].data[0], slices.channels[ch].length[0]);
 *             // Process slice 1 (if wrapping)
 *             if (slices.channels[ch].length[1] > 0) {
 *                 processSamples(ch, (float*)slices.channels[ch].data[1], slices.channels[ch].length[1]);
 *             }
 *         }
 *         sampleIndex += samplesToRead;
 *     }
 * }
 * ```
 */

#pragma once

#include "FlowReader.hpp"
#include "Timing.hpp"

namespace mxl::lib
{
    /**
     * FlowReader specialization for continuous flows (AUDIO).
     * Provides the Samples API for accessing sample buffers across all channels.
     *
     * Pure virtual (abstract base); concrete implementation provided by internal class.
     */
    class MXL_EXPORT ContinuousFlowReader : public FlowReader
    {
    public:
        /**
         * Wait for a specific sample index to become available (blocking, does not return sample data).
         *
         * Use case: Check availability before expensive processing operations.
         *
         * @param index Absolute sample index to wait for (same across all channels)
         * @param deadline Absolute deadline (Clock::Realtime) to stop waiting
         *
         * @return MXL_STATUS_SUCCESS if sample available
         * @return MXL_ERR_OUT_OF_RANGE_TOO_LATE if sample overwritten (index < tail)
         * @return MXL_ERR_OUT_OF_RANGE_TOO_EARLY if sample not yet written (even after waiting)
         * @return MXL_ERR_* other errors
         *
         * Note: Never returns MXL_ERR_TIMEOUT explicitly; timeouts manifest as TOO_EARLY.
         * Warning: No guarantee samples remain valid after return (writer may overwrite).
         */
        virtual mxlStatus waitForSamples(std::uint64_t index, Timepoint deadline) const = 0;

        /**
         * Get a range of samples across all channels (blocking, with deadline).
         *
         * This is the primary sample access method. Waits for sample availability,
         * and returns zero-copy pointers to sample data across all channels.
         *
         * @param index Starting sample index (absolute, same across all channels)
         * @param count Number of samples to retrieve per channel
         * @param deadline Absolute deadline (Clock::Realtime) to stop waiting
         * @param[out] payloadBufferSlices Wrapped buffer slices (handles ring buffer wrap-around).
         *                                  Filled with pointers to sample data for each channel.
         *                                  Each channel may have 1-2 slices (2 if wrapping around).
         *
         * @return MXL_STATUS_SUCCESS if samples retrieved successfully
         * @return MXL_ERR_OUT_OF_RANGE_TOO_LATE if samples overwritten (index < tail)
         * @return MXL_ERR_OUT_OF_RANGE_TOO_EARLY if samples not available (even after waiting)
         * @return MXL_ERR_* other errors
         *
         * Lifetime: Pointers in payloadBufferSlices valid until next getSamples() call or reader destruction.
         * Warning: No guarantee samples remain valid after return (writer may overwrite).
         * Recommendation: Process quickly or copy if long-term storage needed.
         */
        virtual mxlStatus getSamples(std::uint64_t index, std::size_t count, Timepoint deadline, mxlWrappedMultiBufferSlice& payloadBufferSlices) = 0;

        /**
         * Get a range of samples across all channels (non-blocking, returns immediately if not available).
         *
         * Use case: Polling-based access, or when deadline management is external.
         *
         * @param index Starting sample index (absolute, same across all channels)
         * @param count Number of samples to retrieve per channel
         * @param[out] payloadBufferSlices Wrapped buffer slices for zero-copy access
         *
         * @return MXL_STATUS_SUCCESS if samples available
         * @return MXL_ERR_OUT_OF_RANGE_TOO_LATE if samples overwritten
         * @return MXL_ERR_OUT_OF_RANGE_TOO_EARLY if samples not yet written (no waiting)
         * @return MXL_ERR_* other errors
         *
         * Lifetime: Pointers valid until next getSamples() call or reader destruction.
         * Warning: No guarantee samples remain valid after return.
         */
        virtual mxlStatus getSamples(std::uint64_t index, std::size_t count, mxlWrappedMultiBufferSlice& payloadBufferSlices) = 0;

    protected:
        // Inherit FlowReader constructors
        using FlowReader::FlowReader;
    };
}
