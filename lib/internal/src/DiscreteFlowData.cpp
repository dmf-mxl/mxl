// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/DiscreteFlowData.hpp"
#include <cstdint>
#include <stdexcept>
#include <fmt/format.h>

namespace mxl::lib
{
    bool DiscreteFlowData::openContiguousWindow(std::size_t count, std::size_t grainStride) noexcept
    {
        _window = ContiguousWindow{count, grainStride};
        return _window.valid();
    }

    Grain* DiscreteFlowData::emplaceGrainAt(std::size_t i, char const* grainFilePath, std::size_t grainPayloadSize)
    {
        auto const mode = this->created() ? AccessMode::CREATE_READ_WRITE : this->accessMode();
        auto* const target = _window.slot(i);
        if (target == nullptr)
        {
            throw std::out_of_range{fmt::format("Grain index {} is outside the reserved contiguous window.", i)};
        }

        auto grain = SharedMemoryInstance<Grain>{grainFilePath, mode, grainPayloadSize, LockMode::Shared, target, _window.stride()};

        if (!this->created())
        {
            // Check for the version of the grain data structure in the memory that was just mapped.
            if (grain.get()->header.info.version != GRAIN_HEADER_VERSION)
            {
                throw std::invalid_argument{
                    fmt::format("Unsupported grain version: {}, supported version is: {}", grain.get()->header.info.version, GRAIN_HEADER_VERSION)};
            }
        }

        return _grains.emplace_back(std::move(grain)).get();
    }

    void DiscreteFlowData::resetContiguousWindow() noexcept
    {
        _grains.clear();
        _window = {};
    }
}
