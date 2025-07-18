// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#include "Timing.hpp"
#include "detail/ClockHelpers.hpp"

namespace mxl::lib
{
    Timepoint currentTime(Clock clock) noexcept
    {
        auto result = Timepoint{};

        std::timespec ts;
        if (::clock_gettime(detail::clockToId(clock), &ts) == 0)
        {
            result = asTimepoint(ts) + detail::getClockOffset(clock);
        }
        return result;
    }

    Timepoint currentTimeUTC() noexcept
    {
        auto result = Timepoint{};

        std::timespec ts;
        if (::timespec_get(&ts, TIME_UTC) != 0)
        {
            result = asTimepoint(ts);
        }
        return result;
    }
}