// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file DiscreteFlowReader.hpp
 * @brief FlowReader specialization for discrete flows (VIDEO/DATA grains)
 *
 * Provides the Grain API for reading individual grains (frames, packets) from
 * VIDEO and DATA flows. Each grain is accessed by its absolute index.
 *
 * KEY CONCEPTS:
 *
 * Grain indexing:
 * - Each grain has an absolute index (0, 1, 2, 3, ...)
 * - Ring buffer position = grainIndex % grainCount
 * - Example: 16-grain buffer, requesting index 20 returns grain at position 20%16=4
 *
 * Tail index:
 * - Oldest available grain in ring buffer
 * - If writer is at grain 100 with 16-grain buffer, tail = 100-16+1 = 85
 * - Requesting grain < tail returns MXL_ERR_OUT_OF_RANGE_TOO_LATE (overwritten)
 * - Requesting grain > head returns MXL_ERR_OUT_OF_RANGE_TOO_EARLY (not yet written)
 *
 * Partial grains (slices):
 * - Video grains can be committed slice-by-slice (scan line by scan line)
 * - in_minValidSlices specifies how many slices must be ready
 * - Example: 1080p video, request minValidSlices=540 to get half a frame
 * - Enables line-by-line processing pipelines (lower latency)
 *
 * Waiting semantics:
 * - waitForGrain(): Waits using futex, returns when grain available or timeout
 * - getGrain(deadline): Waits + returns grain data
 * - getGrain(no deadline): Non-blocking, returns immediately if grain not ready
 *
 * Zero-copy access:
 * - out_payload points directly into mmap'd grain file
 * - No memcpy; payload valid until next getGrain() call or reader destruction
 * - Application must not modify payload (PROT_READ mapping)
 *
 * TYPICAL USAGE:
 *
 * ```cpp
 * auto* reader = instance->getFlowReader(flowId);
 * auto discreteReader = dynamic_cast<DiscreteFlowReader*>(reader);
 *
 * uint64_t grainIndex = 0;
 * while (running) {
 *     mxlGrainInfo grainInfo;
 *     uint8_t* payload;
 *     auto deadline = currentTime(Clock::Realtime) + fromMilliSeconds(100);
 *
 *     // Wait for next grain (all slices)
 *     auto status = discreteReader->getGrain(grainIndex, totalSlices, deadline, &grainInfo, &payload);
 *     if (status == MXL_STATUS_SUCCESS) {
 *         processGrain(&grainInfo, payload);
 *         grainIndex++;
 *     } else if (status == MXL_ERR_OUT_OF_RANGE_TOO_LATE) {
 *         // Missed grains (buffer overrun), skip ahead to tail
 *         grainIndex = getCurrentTailIndex();
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
     * FlowReader specialization for discrete flows (VIDEO/DATA).
     * Provides the Grain API for accessing individual grains.
     *
     * Pure virtual (abstract base); concrete implementation provided by internal class.
     */
    class MXL_EXPORT DiscreteFlowReader : public FlowReader
    {
    public:
        /**
         * Wait for a specific grain to become available (blocking, does not return grain data).
         *
         * Use case: Check availability before expensive mapping/processing operations.
         * Does NOT update flow access time (unlike getGrain which does).
         *
         * @param in_index Absolute grain index to wait for
         * @param in_minValidSlices Minimum number of slices (scan lines) required.
         *                          Pass total slice count to wait for full grain.
         *                          Pass partial count for slice-by-slice processing.
         * @param in_deadline Absolute deadline (Clock::Realtime) to stop waiting
         *
         * @return MXL_STATUS_SUCCESS if grain available with enough slices
         * @return MXL_ERR_OUT_OF_RANGE_TOO_LATE if grain overwritten (index < tail)
         * @return MXL_ERR_OUT_OF_RANGE_TOO_EARLY if grain not yet written (even after waiting until deadline)
         * @return MXL_ERR_* other errors (flow invalid, etc.)
         *
         * Note: Never returns MXL_ERR_TIMEOUT explicitly; timeouts manifest as TOO_EARLY
         * (grain still not available after waiting).
         */
        virtual mxlStatus waitForGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline) const = 0;

        /**
         * Get a specific grain (blocking, with deadline, returns grain data).
         *
         * This is the primary grain access method. Waits for grain availability,
         * maps grain file if needed, and returns zero-copy pointers to metadata and payload.
         *
         * @param in_index Absolute grain index to retrieve
         * @param in_minValidSlices Minimum number of valid slices required
         * @param in_deadline Absolute deadline (Clock::Realtime) to stop waiting
         * @param[out] out_grainInfo Grain metadata (timestamps, size, slice count, etc.) copied here
         * @param[out] out_payload Set to point to grain payload (zero-copy, read-only)
         *
         * @return MXL_STATUS_SUCCESS if grain retrieved successfully
         * @return MXL_ERR_OUT_OF_RANGE_TOO_LATE if grain overwritten (index < tail)
         * @return MXL_ERR_OUT_OF_RANGE_TOO_EARLY if grain not available (even after waiting)
         * @return MXL_ERR_* other errors
         *
         * Lifetime: out_payload valid until next getGrain() call or reader destruction.
         * Side effect: Updates flow access time (for garbage collection tracking).
         */
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline, mxlGrainInfo* out_grainInfo,
            std::uint8_t** out_payload) = 0;

        /**
         * Get a specific grain (non-blocking, returns immediately if not available).
         *
         * Use case: Polling-based access, or when deadline management is external.
         *
         * @param in_index Absolute grain index to retrieve
         * @param in_minValidSlices Minimum number of valid slices required
         * @param[out] out_grainInfo Grain metadata copied here
         * @param[out] out_payload Set to point to grain payload (zero-copy, read-only)
         *
         * @return MXL_STATUS_SUCCESS if grain available
         * @return MXL_ERR_OUT_OF_RANGE_TOO_LATE if grain overwritten
         * @return MXL_ERR_OUT_OF_RANGE_TOO_EARLY if grain not yet written (no waiting)
         * @return MXL_ERR_* other errors
         *
         * Lifetime: out_payload valid until next getGrain() call or reader destruction.
         * Side effect: Updates flow access time.
         */
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, mxlGrainInfo* out_grainInfo,
            std::uint8_t** out_payload) = 0;

    protected:
        // Inherit FlowReader constructors
        using FlowReader::FlowReader;
    };
}
