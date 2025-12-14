// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "ImmData.hpp"
#include <cstdint>
#include <bit>

namespace mxl::lib::fabrics::ofi
{
    ImmDataGrain::ImmDataGrain(std::uint32_t data) noexcept
    {
        _inner = data;
    }

    ImmDataGrain::ImmDataGrain(std::uint64_t index, std::uint16_t sliceIndex) noexcept
    {
        auto packed = std::bit_cast<ImmDataPacked*>(&_inner);
        packed->ringBufferIndex = index;
        packed->sliceIndex = sliceIndex;
    }

    std::pair<std::uint16_t, std::uint16_t> ImmDataGrain::unpack() const noexcept
    {
        auto packed = std::bit_cast<ImmDataPacked*>(&_inner);
        return {packed->ringBufferIndex, packed->sliceIndex};
    }

    std::uint32_t ImmDataGrain::data() const noexcept
    {
        return _inner;
    }
}
