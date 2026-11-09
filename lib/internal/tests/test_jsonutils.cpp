// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "mxl-internal/JsonUtils.hpp"

using mxl::lib::JsonLimits;
using mxl::lib::jsonToInteger;
using mxl::lib::parseJson;

namespace
{
    std::string nestedArrays(std::size_t depth)
    {
        return std::string(depth, '[') + std::string(depth, ']');
    }

    std::string arrayOf(std::size_t count)
    {
        auto result = std::string{"["};
        for (auto i = std::size_t{0}; i < count; ++i)
        {
            result += (i == 0U) ? "0" : ",0";
        }
        return result + "]";
    }

    std::string objectOf(std::size_t count)
    {
        auto result = std::string{"{"};
        for (auto i = std::size_t{0}; i < count; ++i)
        {
            result += ((i == 0U) ? "\"k" : ",\"k") + std::to_string(i) + "\":0";
        }
        return result + "}";
    }

    picojson::value number(double value)
    {
        return picojson::value{value};
    }
}

TEST_CASE("parseJson : accepts a valid document", "[json]")
{
    auto value = picojson::value{};
    REQUIRE(parseJson(value,
        R"( {"a": [1, "two", true, null], "b": {"c": 2.5}} )"
        "\n\t")
            .empty());
    REQUIRE(value.is<picojson::object>());
    auto const& root = value.get<picojson::object>();
    REQUIRE(root.at("a").get<picojson::array>().size() == 4U);
    REQUIRE(root.at("b").get<picojson::object>().at("c").get<double>() == 2.5);
}

TEST_CASE("parseJson : rejects syntax errors and trailing content", "[json]")
{
    auto value = picojson::value{};
    REQUIRE_FALSE(parseJson(value, "{ this is not json").empty());
    REQUIRE_FALSE(parseJson(value, "").empty());
    REQUIRE_FALSE(parseJson(value, "{} {}").empty());
    REQUIRE_FALSE(parseJson(value, "{\"a\":1} garbage").empty());
}

TEST_CASE("parseJson : limits the input size", "[json]")
{
    auto const limits = JsonLimits{.maxInputSize = 16U};
    auto value = picojson::value{};
    REQUIRE(parseJson(value, R"({"a":"12345678"})", limits).empty()); // 16 bytes
    REQUIRE_FALSE(parseJson(value, R"({"a":"123456789"})", limits).empty());

    // The default limit rejects the 4 MB description string accepted in the security audit.
    auto const large = R"({"description":")" + std::string(4U * 1024U * 1024U, 'x') + R"("})";
    REQUIRE_FALSE(parseJson(value, large).empty());
}

TEST_CASE("parseJson : limits the nesting depth", "[json]")
{
    auto const limits = JsonLimits{};
    auto value = picojson::value{};
    REQUIRE(parseJson(value, nestedArrays(limits.maxDepth)).empty());
    REQUIRE_FALSE(parseJson(value, nestedArrays(limits.maxDepth + 1U)).empty());
    REQUIRE_FALSE(parseJson(value, R"({"a":)" + nestedArrays(limits.maxDepth) + "}").empty());

    // Deep enough to overflow the stack if picojson recursed into all of it.
    auto const deep = JsonLimits{.maxInputSize = 1U << 22U};
    REQUIRE_FALSE(parseJson(value, nestedArrays(1U << 20U), deep).empty());
}

TEST_CASE("parseJson : limits the container size", "[json]")
{
    auto const limits = JsonLimits{};
    auto value = picojson::value{};
    REQUIRE(parseJson(value, arrayOf(limits.maxContainerSize)).empty());
    REQUIRE_FALSE(parseJson(value, arrayOf(limits.maxContainerSize + 1U)).empty());
    REQUIRE(parseJson(value, objectOf(limits.maxContainerSize)).empty());
    REQUIRE_FALSE(parseJson(value, objectOf(limits.maxContainerSize + 1U)).empty());
}

TEST_CASE("parseJson : rejects duplicate keys", "[json]")
{
    auto value = picojson::value{};
    REQUIRE_FALSE(parseJson(value, R"({"a":1,"a":2})").empty());
    REQUIRE_FALSE(parseJson(value, R"({"b":{"a":1,"a":1}})").empty());
    REQUIRE(parseJson(value, R"({"a":{"a":1}})").empty());
}

TEST_CASE("parseJson : reports a number out of double range as an error", "[json]")
{
    auto value = picojson::value{};
    REQUIRE_FALSE(parseJson(value, "[1e400]").empty());
    REQUIRE_FALSE(parseJson(value, "[-1e400]").empty());
}

TEST_CASE("jsonToInteger : converts whole numbers in range", "[json]")
{
    REQUIRE(jsonToInteger<std::uint32_t>(number(0.0), "v") == 0U);
    REQUIRE(jsonToInteger<std::uint32_t>(number(4294967295.0), "v") == UINT32_MAX);
    REQUIRE(jsonToInteger<std::int64_t>(number(-9223372036854775808.0), "v") == INT64_MIN);
    REQUIRE(jsonToInteger<std::int64_t>(number(-1001.0), "v") == -1001);
    REQUIRE(jsonToInteger<std::uint64_t>(number(std::ldexp(1.0, 63)), "v") == (std::uint64_t{1} << 63U));
}

TEST_CASE("jsonToInteger : rejects values that do not convert exactly", "[json]")
{
    REQUIRE_THROWS_AS(jsonToInteger<std::uint32_t>(number(1.5), "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonToInteger<std::uint32_t>(number(-1.0), "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonToInteger<std::uint32_t>(number(4294967296.0), "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonToInteger<std::size_t>(number(1e300), "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonToInteger<std::uint64_t>(number(std::ldexp(1.0, 64)), "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonToInteger<std::int64_t>(number(std::ldexp(1.0, 63)), "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonToInteger<std::int64_t>(number(-std::ldexp(1.0, 64)), "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonToInteger<std::uint32_t>(picojson::value{"12"}, "v"), std::invalid_argument);
    REQUIRE_THROWS_AS(jsonToInteger<std::uint32_t>(picojson::value{}, "v"), std::invalid_argument);
}
