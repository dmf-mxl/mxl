// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/PayloadStorage.hpp"
#include <stdexcept>
#include <fmt/format.h>
#include "mxl-internal/DiscreteFlowData.hpp"

namespace mxl::lib
{
    PayloadStorage::~PayloadStorage() = default;

    HostPayloadStorage::HostPayloadStorage(DiscreteFlowData& flowData)
        : _slotPayloads{}
    {
        auto const count = flowData.grainCount();
        _slotPayloads.reserve(count);
        for (auto slot = std::size_t{0}; slot < count; ++slot)
        {
            // The payload directly follows the grain header in the grain segment.
            _slotPayloads.push_back(reinterpret_cast<std::uint8_t*>(&flowData.grainAt(slot)->header + 1));
        }
    }

    std::size_t HostPayloadStorage::slotCount() const noexcept
    {
        return _slotPayloads.size();
    }

    std::uint8_t* HostPayloadStorage::hostPayload(std::size_t slot) const
    {
        if (slot >= _slotPayloads.size())
        {
            throw std::out_of_range{fmt::format("Slot {} is out of range, the storage has {} slots.", slot, _slotPayloads.size())};
        }
        return _slotPayloads[slot];
    }
}
