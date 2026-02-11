// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowSynchronizationGroup.cpp
 * @brief Manages synchronized reading from multiple flows with different arrival times
 *
 * FlowSynchronizationGroup enables frame-accurate multi-flow synchronization by:
 * - Waiting for data at a specific origin timestamp across multiple flows
 * - Adapting to flows with different source delays (network jitter, processing)
 * - Optimizing wait order to minimize blocking time
 *
 * Problem being solved:
 * In multi-flow scenarios (e.g., video + audio + ancillary data), different flows
 * may arrive with different delays due to network paths, processing chains, etc.
 * Naively waiting for flows in a fixed order would mean waiting for the slowest
 * flow first every time, even if faster flows have already arrived.
 *
 * Solution - adaptive ordering:
 * The group tracks the maximum observed source delay for each flow and reorders
 * the wait list so flows with historically longer delays are checked first. This
 * reduces the number of blocking futex waits by checking flows most likely to be
 * ready last.
 *
 * Use case:
 * @code
 *   auto videoReader = instance->getFlowReader(videoFlowId);
 *   auto audioReader = instance->getFlowReader(audioFlowId);
 *   auto group = instance->createFlowSynchronizationGroup();
 *
 *   group->addReader(*videoReader, minSlices);
 *   group->addReader(*audioReader);
 *
 *   // Wait for both flows to have data at the same origin time
 *   group->waitForDataAt(originTime, deadline);
 *
 *   // Now both video and audio are ready for the same time point
 *   videoReader->getGrain(grainIndex, ...);
 *   audioReader->getSamples(sampleIndex, ...);
 * @endcode
 */

#include "mxl-internal/FlowSynchronizationGroup.hpp"
#include <mxl/time.h>
#include "mxl-internal/ContinuousFlowReader.hpp"
#include "mxl-internal/DiscreteFlowReader.hpp"
#include "mxl-internal/IndexConversion.hpp"

namespace mxl::lib
{
    /**
     * @brief Base constructor for a list entry (common initialization)
     *
     * @param reader The flow reader to track
     * @param variant Whether this is a discrete or continuous flow
     */
    FlowSynchronizationGroup::ListEntry::ListEntry(FlowReader const& reader, Variant variant)
        : reader{&reader}
        , minValidSlices{}
        , variant{variant}
        , maxObservedSourceDelay{}
    {
        // Cache the grain rate for timestamp-to-index conversion
        auto const configInfo = reader.getFlowConfigInfo();
        grainRate = configInfo.common.grainRate;
    }

    /**
     * @brief Construct a discrete flow entry
     *
     * @param reader The discrete flow reader
     * @param minValidSlices Minimum slices required for the grain to be considered ready
     */
    FlowSynchronizationGroup::ListEntry::ListEntry(DiscreteFlowReader const& reader, std::uint16_t minValidSlices)
        : ListEntry{reader, Variant::Discrete}
    {
        this->minValidSlices = minValidSlices;
    }

    /**
     * @brief Construct a continuous flow entry
     *
     * @param reader The continuous flow reader (audio)
     */
    FlowSynchronizationGroup::ListEntry::ListEntry(ContinuousFlowReader const& reader)
        : ListEntry{reader, Variant::Continuous}
    {}

    /**
     * @brief Add a discrete flow reader to the synchronization group
     *
     * If the reader is already in the group, its minValidSlices is updated.
     * Otherwise it's added to the end of the list (ordering will be optimized
     * automatically during waitForDataAt).
     *
     * @param reader The discrete flow reader to add
     * @param minValidSlices Minimum valid slices required for sync (e.g., full frame vs partial)
     *
     * @note The group does not take ownership - caller must ensure reader lifetime
     */
    void FlowSynchronizationGroup::addReader(DiscreteFlowReader const& reader, std::uint16_t minValidSlices)
    {
        // Search for existing entry to update
        auto prev = _readers.before_begin();
        for (auto current = std::next(prev); current != _readers.end(); ++current)
        {
            if (current->reader == &reader)
            {
                // Reader already in group, just update minValidSlices
                current->minValidSlices = minValidSlices;
                return;
            }
            prev = current;
        }

        // Not found, add new entry at the end
        _readers.emplace_after(prev, reader, minValidSlices);
    }

    /**
     * @brief Add a continuous flow reader to the synchronization group
     *
     * If the reader is already in the group, this is a no-op.
     *
     * @param reader The continuous flow reader to add (audio)
     *
     * @note The group does not take ownership - caller must ensure reader lifetime
     */
    void FlowSynchronizationGroup::addReader(ContinuousFlowReader const& reader)
    {
        // Search for existing entry
        auto prev = _readers.before_begin();
        for (auto current = std::next(prev); current != _readers.end(); ++current)
        {
            if (current->reader == &reader)
            {
                // Reader already in group, nothing to do
                return;
            }
            prev = current;
        }

        // Not found, add new entry at the end
        _readers.emplace_after(prev, reader);
    }

    /**
     * @brief Remove a reader from the synchronization group
     *
     * If the reader is not in the group, this is a no-op.
     *
     * @param reader The reader to remove (discrete or continuous)
     */
    void FlowSynchronizationGroup::removeReader(FlowReader const& reader)
    {
        auto prev = _readers.before_begin();
        for (auto current = std::next(prev); current != _readers.end(); ++current)
        {
            if (current->reader == &reader)
            {
                _readers.erase_after(prev);
                return;
            }
            prev = current;
        }
        // Not found - nothing to do
    }

    /**
     * @brief Wait for all flows in the group to have data at a specific origin time
     *
     * This is the core synchronization primitive. It waits for each flow in the group
     * to have data available at the specified origin timestamp, returning when all flows
     * are ready or when the deadline expires.
     *
     * Adaptive ordering optimization:
     * As flows complete, we measure their actual source delay (how late they arrived
     * relative to expected time). Flows with larger delays are moved to the front of
     * the list, so they're checked first in future calls. This minimizes blocking time
     * by checking the slowest flows first.
     *
     * Algorithm:
     * 1. For each flow, convert origin time to expected index based on grain rate
     * 2. Check if data is already available (headIndex >= expectedIndex)
     * 3. If not, block waiting for the data (futex wait with deadline)
     * 4. On success, measure source delay and reorder if needed
     * 5. If any flow fails to meet deadline, return immediately with error
     *
     * @param originTime The media origin timestamp to synchronize on (TAI nanoseconds)
     * @param deadline The absolute time to give up waiting (TAI nanoseconds)
     * @return MXL_STATUS_OK if all flows ready, error code otherwise
     *
     * @note This is const because it doesn't modify the logical state (only internal ordering)
     * @note The _readers list is mutable to allow reordering optimization
     */
    mxlStatus FlowSynchronizationGroup::waitForDataAt(Timepoint originTime, Timepoint deadline) const
    {
        // Iterate through all readers, checking/waiting for each
        // Note: We increment iterator before using 'current' because splice_after
        // may move 'current' to the front, invalidating iteration
        for (auto it = _readers.begin(); it != _readers.end(); /* Incremented in loop */)
        {
            auto const current = it++;

            // Convert origin time to grain/sample index for this flow
            auto const expectedIndex = timestampToIndex(current->grainRate, originTime);
            auto const runtimeInfo = current->reader->getFlowRuntimeInfo();

            // Check if we need to wait (data not yet available)
            if (expectedIndex > runtimeInfo.headIndex)
            {
                // Data not ready yet - must wait
                auto result = mxlStatus{MXL_ERR_UNKNOWN};

                // Dispatch to appropriate wait method based on flow type
                switch (current->variant)
                {
                    case Variant::Discrete:
                        // Wait for grain with minimum valid slices
                        result =
                            static_cast<DiscreteFlowReader const*>(current->reader)->waitForGrain(expectedIndex, current->minValidSlices, deadline);
                        break;

                    case Variant::Continuous:
                        // Wait for samples
                        result = static_cast<ContinuousFlowReader const*>(current->reader)->waitForSamples(expectedIndex, deadline);
                        break;
                }

                if (result == MXL_STATUS_OK)
                {
                    //
                    // Success - perform adaptive reordering optimization
                    //
                    // If the current source delay of this flow exceeds any previously observed
                    // source delay for this flow, we update the cached maximum. If this new
                    // maximum is bigger than the maximum source delay observed for the flow at
                    // the head of the list, we move this flow to the front. This optimizes
                    // future calls by checking the slowest flows first, reducing blocking time.
                    //
                    auto const expectedArrivalTime = indexToTimestamp(current->grainRate, expectedIndex);
                    auto const currentTaiTime = currentTime(Clock::TAI);

                    if (currentTaiTime > expectedArrivalTime)
                    {
                        // Calculate how late this flow arrived
                        auto const sourceDelay = (currentTaiTime - expectedArrivalTime).value;

                        if (sourceDelay > current->maxObservedSourceDelay)
                        {
                            // New maximum delay for this flow
                            current->maxObservedSourceDelay = sourceDelay;

                            // If this flow is now slower than the current head, move it to front
                            if (current->maxObservedSourceDelay > _readers.begin()->maxObservedSourceDelay)
                            {
                                _readers.splice_after(_readers.before_begin(), _readers, current);
                            }
                        }
                    }
                }
                else
                {
                    // Wait failed (timeout, invalid flow, etc.) - abort immediately
                    return result;
                }
            }
            // else: Data already available, continue to next flow
        }

        // All flows have data ready at the requested origin time
        return MXL_STATUS_OK;
    }
}
