// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <mxl/rational.h>
#include "Timing.hpp"

namespace mxl::lib
{
    constexpr std::uint64_t timestampToIndex(mxlRational const& editRate, Timepoint timestamp) noexcept
    {
        if ((editRate.denominator != 0) && (editRate.numerator != 0))
        {
            return static_cast<std::uint64_t>((timestamp.value * __int128_t{editRate.numerator} + 500'000'000 * __int128_t{editRate.denominator}) /
                                              (1'000'000'000 * __int128_t{editRate.denominator}));
        }
        else
        {
            return MXL_UNDEFINED_INDEX;
        }
    }

    constexpr Timepoint indexToTimestamp(mxlRational const& editRate, std::uint64_t index) noexcept
    {
        // Validate the edit rate
        if ((editRate.denominator != 0) && (editRate.numerator != 0))
        {
            return Timepoint{static_cast<std::int64_t>(
                (index * __int128_t{editRate.denominator} * 1'000'000'000 + __int128_t{editRate.numerator} / 2) / __int128_t{editRate.numerator})};
        }
        return {};
    }
}
