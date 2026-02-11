// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowSynchronizationGroup.hpp
 * @brief Multi-flow synchronization for coordinated A/V reading
 *
 * FlowSynchronizationGroup enables an application to wait for data availability
 * across multiple flows simultaneously, ensuring time-aligned media access.
 *
 * USE CASE:
 * An A/V processing application reads video, audio, and ancillary data flows that
 * must be processed in lockstep. Rather than waiting on each flow individually
 * (sequential waits = latency accumulation), the application adds all readers to
 * a sync group and waits once for all flows to have data at a specific timestamp.
 *
 * Example:
 * - Video flow: 1920x1080 @ 24000/1001 fps
 * - Audio flow: 48kHz stereo
 * - Data flow: Ancillary (timecode, captions)
 *
 * Application wants frame 100 (timestamp T):
 * 1. Add all readers to sync group
 * 2. Call waitForDataAt(originTime=T, deadline)
 * 3. Group waits until ALL flows have data at time T (or deadline expires)
 * 4. Application reads from all flows knowing they're synchronized
 *
 * KEY FEATURES:
 * - Weak references: Readers can be destroyed without breaking the group
 * - Timestamp-based: Converts originTime to grain/sample indices per flow
 * - Adaptive delays: Tracks maxObservedSourceDelay to account for jitter
 * - Mixed flow types: Supports both discrete (grains) and continuous (samples)
 *
 * DESIGN:
 * - waitForDataAt() converts originTime to indices using each flow's grain/sample rate
 * - Waits on each flow's syncCounter futex in sequence (not parallel)
 * - Returns success only if ALL flows have data available
 * - Returns earliest error if any flow fails (TOO_LATE, TOO_EARLY, etc.)
 */

#pragma once

#include <cstdint>
#include <forward_list>
#include <mxl/mxl.h>
#include <mxl/rational.h>
#include "Timing.hpp"

namespace mxl::lib
{
    class ContinuousFlowReader;
    class DiscreteFlowReader;
    class FlowReader;

    /**
     * A set of weak references to flow readers for synchronized multi-flow waiting.
     *
     * Typical usage:
     * ```cpp
     * auto* syncGroup = instance->createFlowSynchronizationGroup();
     * syncGroup->addReader(*videoReader, totalSlices);
     * syncGroup->addReader(*audioReader);
     * syncGroup->addReader(*dataReader, totalSlices);
     *
     * auto originTime = computeFrameTimestamp(frameNumber);
     * auto deadline = currentTime(Clock::Realtime) + fromMilliSeconds(100);
     * auto status = syncGroup->waitForDataAt(originTime, deadline);
     * if (status == MXL_STATUS_SUCCESS) {
     *     // All flows have data at originTime, safe to read
     *     videoReader->getGrain(...);
     *     audioReader->getSamples(...);
     *     dataReader->getGrain(...);
     * }
     * ```
     */
    class MXL_EXPORT FlowSynchronizationGroup
    {
    public:
        /**
         * Add a discrete flow reader (VIDEO/DATA) to the sync group.
         * @param reader Discrete reader to add (weak reference, lifetime managed externally)
         * @param minValidSlices Minimum slices required for this flow to be considered "ready"
         */
        void addReader(DiscreteFlowReader const& reader, std::uint16_t minValidSlices);

        /**
         * Add a continuous flow reader (AUDIO) to the sync group.
         * @param reader Continuous reader to add (weak reference, lifetime managed externally)
         */
        void addReader(ContinuousFlowReader const& reader);

        /**
         * Remove a reader from the sync group.
         * Safe to call even if reader not in group (no-op).
         * @param reader Reader to remove (discrete or continuous)
         */
        void removeReader(FlowReader const& reader);

        /**
         * Wait for all flows in the group to have data at originTime (or until deadline).
         *
         * Algorithm:
         * 1. For each flow, convert originTime to grain/sample index using flow's rate
         * 2. Adjust index by maxObservedSourceDelay (accounts for network jitter)
         * 3. Wait for each flow to have data at computed index
         * 4. Return success if ALL flows ready, or first error encountered
         *
         * @param originTime TAI timestamp at which all flows should have data
         * @param deadline Absolute deadline (Clock::Realtime) to stop waiting
         * @return MXL_STATUS_SUCCESS if all flows have data at originTime
         * @return MXL_ERR_OUT_OF_RANGE_TOO_LATE if any flow's data overwritten
         * @return MXL_ERR_OUT_OF_RANGE_TOO_EARLY if any flow's data not yet available
         * @return MXL_ERR_* other errors
         *
         * Note: If multiple flows have errors, returns the first error encountered
         * (iteration order: forward_list order, typically insertion order).
         */
        mxlStatus waitForDataAt(Timepoint originTime, Timepoint deadline) const;

    private:
        /** Discriminator for discrete vs. continuous flow readers. */
        enum class Variant : std::uint8_t
        {
            Discrete,    // VIDEO/DATA (grains)
            Continuous   // AUDIO (samples)
        };

        /**
         * Internal entry representing one reader in the sync group.
         * Stores reader pointer, flow parameters, and adaptive delay tracking.
         */
        struct ListEntry
        {
        public:
            /**
             * Weak pointer to the reader (not owned, lifetime managed externally).
             * Can be DiscreteFlowReader* or ContinuousFlowReader* depending on variant.
             */
            FlowReader const* reader;

            /**
             * For discrete flows: minimum number of slices required.
             * For continuous flows: unused.
             */
            std::uint16_t minValidSlices;

            /**
             * Type of reader (discrete or continuous).
             * Determines how to interpret reader pointer and wait logic.
             */
            Variant variant;

            /**
             * Cached grain/sample rate for this flow.
             * Used to convert originTime to indices without repeatedly querying flow.
             * Avoids const-cast and shared memory access during wait.
             */
            mxlRational grainRate;

            /**
             * Maximum observed source delay in nanoseconds.
             *
             * Tracks the maximum delay between expected time (based on grain rate)
             * and actual grain timestamp. Accounts for network jitter, source drift,
             * and processing delays. Adaptive: grows as delays are observed, never shrinks.
             *
             * Used to lookahead when waiting: instead of waiting for exact index,
             * waits for index + (maxObservedSourceDelay / frame_period) to tolerate jitter.
             */
            std::int64_t maxObservedSourceDelay;

        public:
            /** Construct entry for discrete flow reader. */
            explicit ListEntry(DiscreteFlowReader const& reader, std::uint16_t minValidSlices);

            /** Construct entry for continuous flow reader. */
            explicit ListEntry(ContinuousFlowReader const& reader);

        private:
            /** Common constructor implementation. */
            explicit ListEntry(FlowReader const& reader, Variant variant);
        };

        /** List of readers in the group (forward_list for efficient add/remove). */
        using ReaderList = std::forward_list<ListEntry>;

    private:
        /**
         * Mutable list of readers in this sync group.
         * Mutable because waitForDataAt() is const but may update maxObservedSourceDelay.
         * Uses forward_list (singly-linked list) for:
         * - O(1) add (push_front)
         * - O(n) remove (acceptable, typically small number of flows)
         * - Low memory overhead (just one pointer per node)
         */
        ReaderList mutable _readers;
    };
}
