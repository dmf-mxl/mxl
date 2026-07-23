// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "DataLayout.hpp"
#include <cassert>
#include <array>
#include "mxl/flowinfo.h"

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
        auto totalLength = std::size_t{0};
        for (auto const s : sliceSizes)
        {
            if (s == 0)
            {
                return totalLength;
            }

            totalLength += s;
        }

        return totalLength;
    }

    std::size_t DataLayout::Discrete::activePlaneCount() const noexcept
    {
        auto count = std::size_t{0};
        for (auto s : sliceSizes)
        {
            if (s == 0)
            {
                break;
            }
            ++count;
        }
        return count;
    }

    std::uint32_t DataLayout::Discrete::planePayloadOffset(std::size_t planeIndex, std::uint32_t grainPayloadOffset) const noexcept
    {
        auto offset = grainPayloadOffset;
        for (std::size_t i = 0; i < planeIndex && i < MXL_MAX_PLANES_PER_GRAIN; ++i)
        {
            if (sliceSizes[i] == 0)
            {
                break;
            }
            offset += static_cast<std::uint32_t>(totalSlices) * sliceSizes[i];
        }
        return offset;
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
