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

    REQUIRE(mxlTimestampToHeadIndex(nullptr, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToHeadIndex(&badRate, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToHeadIndex(&badNumerator, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToHeadIndex(&badDenominator, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToHeadIndex(&goodRate, now) != MXL_UNDEFINED_INDEX);
}

TEST_CASE("Index 0 and 1", "[time]")
{
    auto const rate = Rational{30000, 1001};

    auto const firstIndexTimeNs = 0ULL;
    auto const secondIndexTimeNs = rate.denominator * 1'000'000'000ULL / rate.numerator;

    REQUIRE(mxlTimestampToHeadIndex(&rate, firstIndexTimeNs) == 0);
    REQUIRE(mxlTimestampToHeadIndex(&rate, secondIndexTimeNs) == 1);

    REQUIRE(mxlHeadIndexToTimestamp(&rate, 0) == firstIndexTimeNs);
    REQUIRE(mxlHeadIndexToTimestamp(&rate, 1) == secondIndexTimeNs);
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