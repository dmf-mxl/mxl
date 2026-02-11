// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_time.cpp
 * @brief Unit tests for MXL time and index conversion functions
 *
 * This test suite validates MXL's timing system based on TAI (International Atomic Time)
 * and SMPTE ST 2059 epoch (1970-01-01 00:00:00 TAI).
 *
 * Key concepts tested:
 *   - TAI timestamp to grain/sample index conversion (mxlTimestampToIndex)
 *   - Grain/sample index to TAI timestamp conversion (mxlIndexToTimestamp)
 *   - Invalid rate handling (zero numerator/denominator)
 *   - Roundtrip consistency (timestamp → index → timestamp)
 *   - Frame-accurate timing for video rates (e.g., 30000/1001 for 29.97fps)
 *   - Sleep/wait calculations (mxlGetNsUntilIndex)
 *
 * MXL timing model:
 *   - Index 0 starts at TAI epoch (1970-01-01 00:00:00 TAI)
 *   - Each index advances by (denominator / numerator) seconds
 *   - Example: 30000/1001 rate means ~33.37ms per frame
 *   - Timestamps are in nanoseconds since TAI epoch
 *
 * These tests ensure frame-accurate synchronization across distributed systems.
 */

#include <cstdint>
#include <ctime>
#include <catch2/catch_test_macros.hpp>
#include <mxl/mxl.h>
#include <mxl/time.h>

/**
 * @brief Test that invalid rates are properly rejected
 *
 * Verifies that mxlTimestampToIndex returns MXL_UNDEFINED_INDEX for:
 *   - Null rate pointer
 *   - Zero numerator and denominator
 *   - Zero numerator only (undefined rate)
 *   - Zero denominator only (division by zero)
 *
 * Valid rates must have non-zero numerator and denominator.
 */
TEST_CASE("Invalid Times", "[time]")
{
    auto const badRate = mxlRational{0, 0};
    auto const badNumerator = mxlRational{0, 1001};
    auto const badDenominator = mxlRational{30000, 0};
    auto const goodRate = mxlRational{30000, 1001};

    auto const now = mxlGetTime();

    REQUIRE(mxlTimestampToIndex(nullptr, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&badRate, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&badNumerator, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&badDenominator, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&goodRate, now) != MXL_UNDEFINED_INDEX);
}

/**
 * @brief Test index 0 and 1 alignment with TAI epoch
 *
 * Verifies:
 *   - Index 0 corresponds to timestamp 0 (TAI epoch: 1970-01-01 00:00:00 TAI)
 *   - Index 1 corresponds to one frame period later
 *   - Forward conversion (index → timestamp) matches expected values
 *   - Reverse conversion (timestamp → index) is consistent
 *
 * For 30000/1001 rate (~29.97fps):
 *   - Index 0 at 0 ns
 *   - Index 1 at ~33,366,700 ns (one frame duration)
 */
TEST_CASE("Index 0 and 1", "[time]")
{
    auto const rate = mxlRational{30000, 1001};

    auto const firstIndexTimeNs = 0ULL;
    auto const secondIndexTimeNs = (rate.denominator * 1'000'000'000ULL + (rate.numerator / 2)) / rate.numerator;

    REQUIRE(mxlTimestampToIndex(&rate, firstIndexTimeNs) == 0);
    REQUIRE(mxlTimestampToIndex(&rate, secondIndexTimeNs) == 1);

    REQUIRE(mxlIndexToTimestamp(&rate, 0) == firstIndexTimeNs);
    REQUIRE(mxlIndexToTimestamp(&rate, 1) == secondIndexTimeNs);
}

/**
 * @brief Verify TAI epoch corresponds to 1970-01-01 00:00:00 UTC
 *
 * MXL uses SMPTE ST 2059 TAI epoch which is aligned with Unix epoch.
 * This test confirms that timestamp 0 converts to the expected date/time.
 *
 * Note: TAI differs from UTC by leap seconds (currently ~37 seconds),
 * but the epoch itself (1970-01-01 00:00:00) is the same for both.
 */
TEST_CASE("Test TAI Epoch", "[time]")
{
    auto ts = std::timespec{0, 0};
    tm t;
    gmtime_r(&ts.tv_sec, &t);

    REQUIRE(t.tm_year == 70);   // Years since 1900
    REQUIRE(t.tm_mon == 0);     // January (0-indexed)
    REQUIRE(t.tm_mday == 1);    // First day of month
    REQUIRE(t.tm_hour == 0);
    REQUIRE(t.tm_min == 0);
    REQUIRE(t.tm_sec == 0);
}

/**
 * @brief Test roundtrip conversion with current system time
 *
 * Verifies that timestamp → index → timestamp conversion is consistent:
 *   1. Get current TAI time
 *   2. Convert to index (quantizes to nearest frame boundary)
 *   3. Convert back to timestamp
 *   4. Verify difference is less than one frame duration
 *   5. Verify index remains unchanged in roundtrip
 *
 * Also tests mxlGetNsUntilIndex for future frame timing (used for sleep/wait).
 *
 * Expected precision: Within one frame period (~33.37ms for 29.97fps)
 */
TEST_CASE("Index <-> Timestamp roundtrip (current)", "[time]")
{
    auto const rate = mxlRational{30000, 1001};

    auto const currentTime = mxlGetTime();
    auto const currentIndex = mxlTimestampToIndex(&rate, currentTime);

    auto const timestamp = mxlIndexToTimestamp(&rate, currentIndex);
    auto const calculatedIndex = mxlTimestampToIndex(&rate, timestamp);

    auto const timeDelta = (currentTime > timestamp) ? currentTime - timestamp : timestamp - currentTime;
    REQUIRE(timeDelta < 33'366'667LL);  // Less than one frame @ 29.97fps
    REQUIRE(calculatedIndex == currentIndex);

    REQUIRE(mxlGetNsUntilIndex(currentIndex + 33, &rate) > 0);
}

/**
 * @brief Exhaustive roundtrip test over a large index range
 *
 * Validates perfect roundtrip consistency for 30 million consecutive indices:
 *   - Each index converts to a unique timestamp
 *   - Each timestamp converts back to the original index
 *   - No rounding errors or drift over extended ranges
 *
 * This test covers ~11.6 days of continuous video at 29.97fps
 * (30,000,000 frames ÷ 29.97 fps ÷ 86400 seconds/day).
 *
 * Ensures frame-accurate timing over long-running productions.
 */
TEST_CASE("Index <-> Timestamp roundtrip (others)", "[time]")
{
    auto const editRate = mxlRational{30000, 1001};

    // Test 30 million consecutive frames for consistency
    for (auto i = 30'000'000U; i < 60'000'000U; ++i)
    {
        auto const ts = mxlIndexToTimestamp(&editRate, i);
        auto const rti = mxlTimestampToIndex(&editRate, ts);
        REQUIRE(i == rti);
    }
}
