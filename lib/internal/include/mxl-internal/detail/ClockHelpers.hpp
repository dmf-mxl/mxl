// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ClockHelpers.hpp
 * @brief Platform-specific clock mapping and TAI emulation
 *
 * This file bridges MXL's Clock enum to POSIX clockid_t values, handling platform differences.
 *
 * Key design decisions:
 *
 * 1. CLOCK_MONOTONIC_RAW vs CLOCK_MONOTONIC:
 *    - CLOCK_MONOTONIC_RAW is preferred because it's unaffected by NTP frequency adjustments
 *    - CLOCK_MONOTONIC can be slightly adjusted by NTP (for gentle time synchronization)
 *    - We want truly monotonic time for timeouts and rate calculations
 *    - Fallback to CLOCK_MONOTONIC on systems without _RAW
 *
 * 2. CLOCK_TAI availability:
 *    - Linux kernel 3.10+ provides native CLOCK_TAI (International Atomic Time)
 *    - On older kernels, CLOCK_TAI is undefined at compile time
 *    - We emulate TAI by adding leap seconds offset to CLOCK_REALTIME
 *    - Current leap seconds: 37 (as of 2017, no new leap seconds since then)
 *    - This emulation is imperfect (doesn't track historical leap seconds) but sufficient for
 *      most use cases where all endpoints use the same epoch reference
 *
 * 3. Why TAI for media?
 *    - UTC has leap seconds inserted unpredictably (last one in 2016)
 *    - TAI never jumps backward or has discontinuities
 *    - PTP (Precision Time Protocol) distributes TAI across network
 *    - SMPTE ST 2059 mandates TAI for broadcast synchronization
 */

#pragma once

#include <ctime>
#include "../Timing.hpp"

namespace mxl::lib::detail
{
    /**
     * Map MXL Clock enum to POSIX clockid_t.
     *
     * Platform-specific choices:
     * - Prefer CLOCK_MONOTONIC_RAW (unaffected by NTP) over CLOCK_MONOTONIC
     * - Use native CLOCK_TAI if available (Linux 3.10+)
     * - Return CLOCK_REALTIME as default fallback
     *
     * @param clock MXL Clock enum value
     * @return Corresponding POSIX clockid_t for clock_gettime/clock_nanosleep
     */
    constexpr clockid_t clockToId(Clock clock) noexcept
    {
        switch (clock)
        {
            case Clock::Monotonic:
                // Prefer RAW variant to avoid NTP frequency adjustments
#if defined(CLOCK_MONOTONIC_RAW)
                return CLOCK_MONOTONIC_RAW;
#else
                return CLOCK_MONOTONIC;
#endif

#if defined(CLOCK_TAI)
            // Native TAI support (Linux 3.10+)
            case Clock::TAI: return CLOCK_TAI;
#endif
            // CPU time clocks for profiling/diagnostics
            case Clock::ProcessCPUTime: return CLOCK_PROCESS_CPUTIME_ID;
            case Clock::ThreadCPUTime:  return CLOCK_THREAD_CPUTIME_ID;

            // Default to realtime (includes Clock::Realtime and Clock::TAI on old kernels)
            default:                    return CLOCK_REALTIME;
        }
    }

    /**
     * Get the offset to add to CLOCK_REALTIME to emulate Clock::TAI on old kernels.
     *
     * TAI is ahead of UTC by the number of leap seconds inserted since 1972.
     * As of 2017, there are 37 leap seconds. No leap seconds have been added since then
     * (and discussions are ongoing about abolishing leap seconds entirely).
     *
     * On systems with native CLOCK_TAI, this returns zero (no emulation needed).
     * On systems without CLOCK_TAI, this returns 37 seconds to convert UTC to TAI.
     *
     * Limitation: This doesn't track historical leap second insertions, so timestamps
     * before 2017 will be off. For most real-time media applications, this is acceptable
     * since all endpoints use the same reference.
     *
     * @param clock MXL Clock enum value
     * @return Duration to add to CLOCK_REALTIME, or zero if no offset needed
     */
    constexpr Duration getClockOffset(Clock clock) noexcept
    {
        [[maybe_unused]]
        constexpr auto const ZERO_SECONDS = fromSeconds(0.0);
        [[maybe_unused]]
        constexpr auto const TAI_LEAP_SECONDS = fromSeconds(37.0);  // Current TAI-UTC offset

        switch (clock)
        {
#if !defined(CLOCK_TAI)
            // Emulate TAI by adding leap seconds to CLOCK_REALTIME
            case Clock::TAI: return TAI_LEAP_SECONDS;
#endif
            default: return ZERO_SECONDS;  // No offset needed
        }
    }
}
