// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "DataLayout.hpp"
#include <cassert>
#include <array>
#include "mxl/flowinfo.h"

namespace mxl::lib::fabrics::ofi
{
    DataLayout DataLayout::fromDiscrete(std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> const& sliceSizes) noexcept
    {
        return DataLayout{DataLayout::Discrete{.sliceSizes = sliceSizes}};
    };

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

    DataLayout::Discrete const& DataLayout::asDiscrete() const noexcept
    {
        return std::get<Discrete>(_inner);
    }

    DataLayout::Continuous const& DataLayout::asContinuous() const noexcept
    {
        return std::get<Continuous>(_inner);
    }

    DataLayout::DataLayout(InnerLayout inner) noexcept
        : _inner(inner)
    {}
}
