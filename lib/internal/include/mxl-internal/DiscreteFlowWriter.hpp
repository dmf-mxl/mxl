// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file DiscreteFlowWriter.hpp
 * @brief FlowWriter specialization for discrete flows (VIDEO/DATA grains)
 *
 * Provides the Grain API for writing individual grains to VIDEO and DATA flows.
 * Each grain is accessed by its absolute index, filled by the writer, then committed.
 *
 * WRITE PATTERN:
 * 1. openGrain(index) - Get mutable pointers to grain header and payload
 * 2. Fill grain data (video pixels, ancillary packets, etc.)
 * 3. commit(grainInfo) - Atomically publish grain, wake readers
 * OR: cancel() - Discard changes without publishing
 *
 * PARTIAL WRITES (SLICES):
 * - commit() can be called multiple times per grain (slice-by-slice)
 * - Each commit updates validSlices count and wakes readers
 * - Enables progressive scan-line rendering (lower latency pipelines)
 *
 * Example: 1080p video
 * - openGrain(100) once
 * - commit() after each scan line (1080 commits)
 * - Readers can start processing top of frame before bottom is written
 */

#pragma once

#include "FlowWriter.hpp"

namespace mxl::lib
{
    /**
     * FlowWriter specialization for discrete flows (VIDEO/DATA).
     * Pure virtual (abstract base); concrete implementation provided by internal class.
     */
    class MXL_EXPORT DiscreteFlowWriter : public FlowWriter
    {
    public:
        /**
         * Get grain metadata without opening for write (read-only query).
         * @param in_index Grain index
         * @return Copy of mxlGrainInfo (safe to cache)
         */
        [[nodiscard]]
        virtual mxlGrainInfo getGrainInfo(std::uint64_t in_index) const = 0;

        /**
         * Open a grain for writing (exclusive access).
         * @param in_index Grain ring buffer index
         * @param[out] out_grainInfo Grain metadata (fill timestamps, flags, sizes)
         * @param[out] out_payload Mutable pointer to grain payload (write video data here)
         * @return MXL_STATUS_SUCCESS or error
         */
        virtual mxlStatus openGrain(std::uint64_t in_index, mxlGrainInfo* out_grainInfo, std::uint8_t** out_payload) = 0;

        /**
         * Commit grain changes (publish to readers).
         * Updates metadata, increments syncCounter, wakes readers via futex.
         * Can be called multiple times (slice-by-slice commits).
         * @param mxlGrainInfo Updated grain metadata (with validSlices, timestamp, etc.)
         * @return MXL_STATUS_SUCCESS or error
         */
        virtual mxlStatus commit(mxlGrainInfo const& mxlGrainInfo) = 0;

        /**
         * Cancel grain write (discard changes without publishing).
         * Releases exclusive access without waking readers.
         * @return MXL_STATUS_SUCCESS or error
         */
        virtual mxlStatus cancel() = 0;

    protected:
        using FlowWriter::FlowWriter;
    };
}
