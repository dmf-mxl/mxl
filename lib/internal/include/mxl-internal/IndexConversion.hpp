// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file IndexConversion.hpp
 * @brief Convert between grain indices and TAI timestamps using edit rates
 *
 * In MXL, media flows have an "edit rate" (frame rate expressed as a rational number).
 * Each grain has an index (0, 1, 2, ...) and a corresponding timestamp in TAI nanoseconds.
 *
 * Examples:
 * - 24000/1001 edit rate (23.976 fps): grain 0 = 0ns, grain 1 = 41708333ns, grain 2 = 83416667ns
 * - 30000/1001 edit rate (29.97 fps): grain 0 = 0ns, grain 1 = 33366667ns, grain 2 = 66733333ns
 * - 48000/1 edit rate (48 fps): grain 0 = 0ns, grain 1 = 20833333ns, grain 2 = 41666667ns
 *
 * Why rational edit rates?
 * - Avoids floating-point rounding errors that accumulate over time
 * - Exactly represents NTSC rates (23.976, 29.97, 59.94) as 24000/1001, 30000/1001, 60000/1001
 * - Allows precise frame-accurate synchronization across flows
 *
 * Why __int128_t?
 * - Prevents overflow: timestamp (int64 nanoseconds) * numerator (int32) exceeds int64 range
 * - Maintains precision through intermediate calculations
 * - Result fits back into int64 after division
 *
 * Rounding strategy:
 * - timestampToIndex: rounds to nearest integer (adds 0.5 before division)
 * - indexToTimestamp: rounds to nearest integer (adds numerator/2 before division)
 * - This ensures round-trip consistency: indexToTimestamp(timestampToIndex(t)) ≈ t
 */

#pragma once

#include <cstdint>
#include <mxl/rational.h>
#include "Timing.hpp"

namespace mxl::lib
{
    /**
     * Convert a TAI timestamp to a grain index using the flow's edit rate.
     *
     * Formula: index = round(timestamp_ns * numerator / (1e9 * denominator))
     * Rounds to nearest integer for frame-accurate positioning.
     *
     * @param editRate Flow's frame rate as a rational (e.g., {24000, 1001} for 23.976fps)
     * @param timestamp TAI timestamp in nanoseconds
     * @return Grain index (0-based), or MXL_UNDEFINED_INDEX if editRate is invalid
     *
     * Example: With 24000/1001 edit rate and timestamp 41708333ns → index 1
     */
    constexpr std::uint64_t timestampToIndex(mxlRational const& editRate, Timepoint timestamp) noexcept
    {
        // Validate edit rate (avoid division by zero)
        if ((editRate.denominator != 0) && (editRate.numerator != 0))
        {
            // Use __int128_t to avoid overflow during (timestamp * numerator) multiplication
            // Add 500'000'000 * denominator for round-to-nearest (equivalent to adding 0.5 before division)
            return static_cast<std::uint64_t>((timestamp.value * __int128_t{editRate.numerator} + 500'000'000 * __int128_t{editRate.denominator}) /
                                              (1'000'000'000 * __int128_t{editRate.denominator}));
        }
        else
        {
            return MXL_UNDEFINED_INDEX;
        }
    }

    /**
     * Convert a grain index to its TAI timestamp using the flow's edit rate.
     *
     * Formula: timestamp_ns = round(index * denominator * 1e9 / numerator)
     * Rounds to nearest integer for precision.
     *
     * @param editRate Flow's frame rate as a rational (e.g., {30000, 1001} for 29.97fps)
     * @param index Grain index (0-based)
     * @return TAI timestamp in nanoseconds, or Timepoint{} (zero) if editRate is invalid
     *
     * Example: With 30000/1001 edit rate and index 1 → timestamp 33366667ns
     */
    constexpr Timepoint indexToTimestamp(mxlRational const& editRate, std::uint64_t index) noexcept
    {
        // Validate the edit rate (avoid division by zero)
        if ((editRate.denominator != 0) && (editRate.numerator != 0))
        {
            // Use __int128_t to avoid overflow
            // Add numerator/2 for round-to-nearest
            return Timepoint{static_cast<std::int64_t>(
                (index * __int128_t{editRate.denominator} * 1'000'000'000 + __int128_t{editRate.numerator} / 2) / __int128_t{editRate.numerator})};
        }
        return {};  // Return zero timestamp for invalid edit rate
    }
}
