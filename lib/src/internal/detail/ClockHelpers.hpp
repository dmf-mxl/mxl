// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <ctime>
#include "../Timing.hpp"

namespace mxl::lib::detail
{
    constexpr clockid_t clockToId(Clock clock) noexcept
    {
        switch (clock)
        {
            case Clock::Monotonic:
#if defined(CLOCK_MONOTONIC_RAW)
                return CLOCK_MONOTONIC_RAW;
#else
                return CLOCK_MONOTONIC;
#endif

#if defined(CLOCK_TAI)
            case Clock::TAI: return CLOCK_TAI;
#endif
            case Clock::ProcessCPUTime: return CLOCK_PROCESS_CPUTIME_ID;

            case Clock::ThreadCPUTime:  return CLOCK_THREAD_CPUTIME_ID;

            default:                    return CLOCK_REALTIME;
        }
    }

    constexpr Duration getClockOffset(Clock clock) noexcept
    {
        [[maybe_unused]]
        constexpr auto const ZERO_SECONDS = fromSeconds(0.0);
        [[maybe_unused]]
        constexpr auto const TAI_LEAP_SECONDS = fromSeconds(37.0);

        switch (clock)
        {
#if !defined(CLOCK_TAI)
            case Clock::TAI: return TAI_LEAP_SECONDS;
#endif
            default: return ZERO_SECONDS;
        }
    }
}
