// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Owned mappings and runtime access for event flows.
 */

#pragma once

#include <memory>
#include "EventRingBuffer.hpp"
#include "FlowData.hpp"

namespace mxl::lib
{
    /** @brief Own the common flow mapping, single event ring mapping and ring view. */
    class EventFlowData : public FlowData
    {
    public:
        /**
         * @brief Take ownership of an existing common flow mapping.
         * @param flowSegment Mapping to move into the base FlowData object.
         */
        explicit EventFlowData(SharedMemoryInstance<Flow>&& flowSegment) noexcept
            : FlowData{std::move(flowSegment)}
        {}

        /**
         * @brief Open or create the common flow mapping before attaching the event ring.
         * @param flowFilePath Path to the flow's data file.
         * @param mode Shared-memory access mode.
         * @param lockMode Advisory lock mode for the common flow file.
         * @throws std::exception The file cannot be mapped or locked.
         */
        EventFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode)
            : FlowData{flowFilePath, mode, lockMode}
        {}

        /** @return Configured number of slots, or zero before the ring file is open. */
        constexpr std::size_t eventCount() const noexcept
        {
            return _events ? flowInfo()->config.event.eventCount : 0;
        }

        /**
         * @brief Map the complete ring file and initialize it if newly created.
         * @param eventFilePath Path to the events file for this flow.
         * @pre The common flow configuration contains valid event geometry.
         * @throws std::exception Mapping fails or stored ring geometry is invalid.
         */
        void openEventBuffer(char const* eventFilePath)
        {
            auto const config = flowInfo()->config.event;
            auto const size = EventRingBuffer::bufferSize(config.eventCount, config.eventPayloadSize);
            auto const mode = created() ? AccessMode::CREATE_READ_WRITE : accessMode();
            _events = SharedMemorySegment{eventFilePath, mode, size, LockMode::None};
            if (_events.created())
            {
                EventRingBuffer::initialize(_events.data(), config.eventCount, config.eventPayloadSize);
            }
            _ring = std::make_unique<EventRingBuffer>(_events.data(), config.eventCount, config.eventPayloadSize);
        }

        /**
         * @brief Compute the retained window from the last committed index and capacity.
         * @return Oldest nominally retained index, or zero before the first commit.
         * @note A producer overwriting a slot can make it unavailable before the head advances.
         */
        constexpr std::uint64_t oldestIndex() const noexcept
        {
            auto const head = EventRingBuffer::load(flowInfo()->runtime.headIndex);
            return (head != MXL_UNDEFINED_INDEX) && (head >= eventCount()) ? head - (eventCount() - 1) : 0;
        }

        /** @return Mutable ring view; openEventBuffer must have succeeded. */
        constexpr EventRingBuffer& ring() noexcept
        {
            return *_ring;
        }

        /** @return Read-only ring view; openEventBuffer must have succeeded. */
        constexpr EventRingBuffer const& ring() const noexcept
        {
            return *_ring;
        }

    private:
        SharedMemorySegment _events;            ///< Owner of the single events mapping.
        std::unique_ptr<EventRingBuffer> _ring; ///< Non-owning ring view whose lifetime is bounded by _events.
    };
}
