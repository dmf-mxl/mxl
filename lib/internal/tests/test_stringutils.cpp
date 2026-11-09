// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <stdexcept>
#include <catch2/catch_test_macros.hpp>
#include "mxl-internal/JsonUtils.hpp"
#include "mxl-internal/StringUtils.hpp"

using mxl::lib::jsonStringToInteger;
using mxl::lib::stringToInteger;

TEST_CASE("stringToInteger : converts decimal numbers in range", "[string]")
{
    REQUIRE(stringToInteger<std::uint64_t>("0", "v") == 0U);
    REQUIRE(stringToInteger<std::uint64_t>("18446744073709551615", "v") == UINT64_MAX);
    REQUIRE(stringToInteger<std::uint16_t>("65535", "v") == UINT16_MAX);
    REQUIRE(stringToInteger<std::int64_t>("-9223372036854775808", "v") == INT64_MIN);
}

TEST_CASE("stringToInteger : rejects what std::stoull accepts", "[string]")
{
    for (auto const* text : {"", " 1", "1 ", "+1", "-1", "1x", "0x10", "1.0", "1e3", "18446744073709551616"})
    {
        CAPTURE(text);
        REQUIRE_THROWS_AS(stringToInteger<std::uint64_t>(text, "v"), std::invalid_argument);
    }
    REQUIRE_THROWS_AS(stringToInteger<std::uint16_t>("65536", "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(stringToInteger<std::int64_t>("9223372036854775808", "v"), std::invalid_argument);
}

TEST_CASE("jsonStringToInteger : requires a JSON string", "[string]")
{
    REQUIRE(jsonStringToInteger<std::uint64_t>(picojson::value{"42"}, "v") == 42U);
    REQUIRE_THROWS_AS(jsonStringToInteger<std::uint64_t>(picojson::value{42.0}, "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonStringToInteger<std::uint64_t>(picojson::value{}, "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonStringToInteger<std::uint64_t>(picojson::value{"-1"}, "v"), std::invalid_argument);
}
