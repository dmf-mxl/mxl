#include <cstdint>
#include <ctime>
#include <catch2/catch_test_macros.hpp>
#include "mxl/mxl.h"
#include "mxl/time.h"

TEST_CASE("Invalid Times", "[time]")
{
    auto const badRate = Rational{0, 0};
    auto const badNumerator = Rational{0, 1001};
    auto const badDenominator = Rational{30000, 0};
    auto const goodRate = Rational{30000, 1001};

    auto const now = mxlGetTime();

    REQUIRE(mxlTimestampToIndex(nullptr, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&badRate, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&badNumerator, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&badDenominator, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&goodRate, now) != MXL_UNDEFINED_INDEX);
}

TEST_CASE("Index 0 and 1", "[time]")
{
    auto const rate = Rational{30000, 1001};

    auto const firstIndexTimeNs = 0ULL;
    auto const secondIndexTimeNs = (rate.denominator * 1'000'000'000ULL + (rate.numerator / 2)) / rate.numerator;

    REQUIRE(mxlTimestampToIndex(&rate, firstIndexTimeNs) == 0);
    REQUIRE(mxlTimestampToIndex(&rate, secondIndexTimeNs) == 1);

    REQUIRE(mxlIndexToTimestamp(&rate, 0) == firstIndexTimeNs);
    REQUIRE(mxlIndexToTimestamp(&rate, 1) == secondIndexTimeNs);
}

TEST_CASE("Test TAI Epoch", "[time]")
{
    auto ts = std::timespec{0, 0};
    tm t;
    gmtime_r(&ts.tv_sec, &t);

    REQUIRE(t.tm_year == 70);
    REQUIRE(t.tm_mon == 0);
    REQUIRE(t.tm_mday == 1);
    REQUIRE(t.tm_hour == 0);
    REQUIRE(t.tm_min == 0);
    REQUIRE(t.tm_sec == 0);
}

TEST_CASE("Index <-> Timestamp roundtrip (current)", "[time]")
{
    auto const rate = Rational{30000, 1001};

    auto const currentTime = mxlGetTime();
    auto const currentIndex = mxlGetCurrentIndex(&rate);
    auto const timestamp = mxlIndexToTimestamp(&rate, currentIndex);
    auto const calculatedIndex = mxlTimestampToIndex(&rate, timestamp);

    auto const timeDelta = (currentTime > timestamp) ? currentTime - timestamp : timestamp - currentTime;
    REQUIRE(timeDelta < 500'000'000U);
    REQUIRE(calculatedIndex == currentIndex);
    REQUIRE(mxlGetNsUntilIndex(currentIndex + 33, &rate) > 0);
}

TEST_CASE("Index <-> Timestamp roundtrip (others)", "[time]")
{
    auto const editRate = Rational{30000, 1001};

    for (auto i = 30'000'000U; i < 60'000'000U; ++i)
    {
        auto const ts = mxlIndexToTimestamp(&editRate, i);
        auto const rti = mxlTimestampToIndex(&editRate, ts);
        REQUIRE(i == rti);
    }
}
