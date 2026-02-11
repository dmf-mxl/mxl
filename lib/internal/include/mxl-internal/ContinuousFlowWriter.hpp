// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ContinuousFlowWriter.hpp
 * @brief FlowWriter specialization for continuous flows (AUDIO samples)
 *
 * Provides the Samples API for writing sample buffers to AUDIO flows.
 * Samples are written across all channels simultaneously.
 *
 * WRITE PATTERN:
 * 1. openSamples(index, count) - Get mutable pointers to sample buffers
 * 2. Fill sample data (audio PCM, typically float32)
 * 3. commit() - Atomically publish samples, wake readers
 * OR: cancel() - Discard changes without publishing
 *
 * SAMPLE ALIGNMENT:
 * - All channels written together (same index range for all)
 * - Samples laid out per-channel (not interleaved)
 * - Handles ring buffer wrap-around (up to 2 slices per channel)
 *
 * Example: 48kHz stereo, 1920 samples (one video frame @ 24fps)
 * - openSamples(currentIndex, 1920)
 * - Fill channel 0: 1920 float32 samples
 * - Fill channel 1: 1920 float32 samples
 * - commit() - Readers can now read these samples
 */

#pragma once

#include "FlowWriter.hpp"

namespace mxl::lib
{
    /**
     * FlowWriter specialization for continuous flows (AUDIO).
     * Pure virtual (abstract base); concrete implementation provided by internal class.
     */
    class MXL_EXPORT ContinuousFlowWriter : public FlowWriter
    {
    public:
        /**
         * Open sample buffers for writing across all channels.
         *
         * Returns mutable wrapped buffer slices (handles ring buffer wrap).
         * Each channel may have 1-2 slices (2 if wrapping around).
         *
         * @param index Starting sample index (absolute, same across all channels)
         * @param count Number of samples to write per channel
         * @param[out] payloadBufferSlices Mutable wrapped slices (write audio data here)
         * @return MXL_STATUS_SUCCESS or error
         *
         * Lifetime: Pointers valid until commit() or cancel() called.
         */
        virtual mxlStatus openSamples(std::uint64_t index, std::size_t count, mxlMutableWrappedMultiBufferSlice& payloadBufferSlices) = 0;

        /**
         * Commit sample changes (publish to readers).
         * Updates sampleOffset, increments syncCounter, wakes readers via futex.
         * @return MXL_STATUS_SUCCESS or error
         */
        virtual mxlStatus commit() = 0;

        /**
         * Cancel sample write (discard changes without publishing).
         * Releases exclusive access without waking readers.
         * @return MXL_STATUS_SUCCESS or error
         */
        virtual mxlStatus cancel() = 0;

    protected:
        using FlowWriter::FlowWriter;
    };
}
