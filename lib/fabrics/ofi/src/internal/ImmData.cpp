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
        auto ringBufferIndex = static_cast<std::uint16_t>(index);
        _inner = std::bit_cast<std::uint32_t>(Unpacked{.ringBufferIndex = ringBufferIndex, .sliceIndex = sliceIndex});
    }

    ImmDataGrain::Unpacked ImmDataGrain::unpack() const noexcept
    {
        return std::bit_cast<Unpacked>(_inner);
    }

    std::uint32_t ImmDataGrain::data() const noexcept
    {
        return _inner;
    }
}
