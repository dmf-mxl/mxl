// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "ImmData.hpp"
#include <cstdint>

namespace mxl::lib::fabrics::ofi
{
    ImmDataGrain::ImmDataGrain(std::uint32_t data) noexcept
    {
        _inner.data = data;
    }

    ImmDataGrain::ImmDataGrain(std::uint64_t index, std::uint16_t sliceIndex) noexcept
    {
        _inner.packed.ringBufferIndex = index;
        _inner.packed.sliceIndex = sliceIndex;
    }

    std::pair<std::uint16_t, std::uint16_t> ImmDataGrain::unpack() const noexcept
    {
        return {_inner.packed.ringBufferIndex, _inner.packed.sliceIndex};
    }

    std::uint32_t ImmDataGrain::data() const noexcept
    {
        return _inner.data;
    }
}
