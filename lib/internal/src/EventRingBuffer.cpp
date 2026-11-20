// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/EventRingBuffer.hpp"
#include <memory>

namespace mxl::lib
{
    void EventRingBuffer::initialize(void* memory, std::uint32_t count, std::uint32_t payloadSize)
    {
        validateGeometry(count, payloadSize);
        auto header = std::construct_at(static_cast<EventRingHeader*>(memory));
        header->version = STORAGE_VERSION;
        header->size = sizeof *header;
        header->eventCount = count;
        header->payloadSize = payloadSize;
        auto slotMemory = static_cast<std::uint8_t*>(memory) + sizeof(EventRingHeader);
        auto const stride = slotStride(payloadSize);
        auto const payloadWordCount = (payloadSize + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);
        for (auto i = std::size_t{0}; i < count; ++i, slotMemory += stride)
        {
            auto event = std::construct_at(reinterpret_cast<Event*>(slotMemory));
            event->header.version = STORAGE_VERSION;
            event->header.size = sizeof event->header;
            event->header.sequence = EMPTY;
            std::uninitialized_value_construct_n(payloadWords(*event), payloadWordCount);
        }
    }
}
