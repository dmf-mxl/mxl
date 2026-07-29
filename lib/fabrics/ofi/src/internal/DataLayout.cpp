// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "DataLayout.hpp"
#include <cassert>
#include <algorithm>
#include <array>
#include <numeric>
#include "mxl/flowinfo.h"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{
    DataLayout DataLayout::fromDiscrete(std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> const& sliceSizes, std::uint16_t totalSlices) noexcept
    {
        return DataLayout{
            DataLayout::Discrete{.sliceSizes = sliceSizes, .totalSlices = totalSlices}
        };
    };

    std::size_t DataLayout::Discrete::totalLength() const noexcept
    {
        return std::accumulate(sliceSizes.begin(), sliceSizes.end(), std::size_t{0});
    }

    std::size_t DataLayout::Discrete::activePlaneCount() const noexcept
    {
        return std::ranges::count_if(sliceSizes, [](std::uint32_t sliceSize) { return sliceSize > 0; });
    }

    std::uint32_t DataLayout::Discrete::planePayloadOffset(std::size_t planeIndex, std::uint32_t grainPayloadOffset) const
    {
        if (sliceSizes.size() <= planeIndex)
        {
            throw Exception::invalidState("Invalid plane index {} not in range {}-{}", planeIndex, 0, sliceSizes.size());
        }

        return std::accumulate(sliceSizes.begin(),
            sliceSizes.begin() + planeIndex,
            grainPayloadOffset,
            [this](std::uint32_t lhs, std::uint32_t rhs) { return lhs + (rhs * static_cast<std::uint32_t>(totalSlices)); });
    }

    DataLayout DataLayout::fromContinuous(std::size_t sampleSize, std::size_t channelCount, std::size_t bufferLength) noexcept
    {
        return DataLayout{
            DataLayout::Continuous{.sampleSize = sampleSize, .channelCount = channelCount, .bufferLength = bufferLength}
        };
    }

    bool DataLayout::isDiscrete() const noexcept
    {
        return std::holds_alternative<Discrete>(_inner);
    }

    bool DataLayout::isContinuous() const noexcept
    {
        return std::holds_alternative<Continuous>(_inner);
    }

    DataLayout::Discrete const& DataLayout::asDiscrete() const
    {
        return std::get<Discrete>(_inner);
    }

    DataLayout::Continuous const& DataLayout::asContinuous() const
    {
        return std::get<Continuous>(_inner);
    }

    DataLayout::DataLayout(InnerLayout inner) noexcept
        : _inner(inner)
    {}
}
