// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include "GrainSlices.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("ofi: SliceRange construction", "[ofi][GrainSlices]")
{
    SECTION("valid range")
    {
        auto range = SliceRange::make(10, 20);
        REQUIRE(range.start() == 10);
        REQUIRE(range.end() == 20);
    }

    SECTION("single-element range")
    {
        auto range = SliceRange::make(5, 6);
        REQUIRE(range.start() == 5);
        REQUIRE(range.end() == 6);
    }

    SECTION("empty range (start == end) is valid")
    {
        auto range = SliceRange::make(0, 0);
        REQUIRE(range.start() == 0);
        REQUIRE(range.end() == 0);
    }

    SECTION("inverted range throws")
    {
        REQUIRE_THROWS(SliceRange::make(10, 5));
    }
}

TEST_CASE("ofi: SliceRange transferSize", "[ofi][GrainSlices]")
{
    constexpr auto const payloadOffset = std::uint32_t{8192};
    constexpr auto const sliceSize = std::uint32_t{5120};

    SECTION("from start includes header")
    {
        auto range = SliceRange::make(0, 1080);
        REQUIRE(range.transferSize(payloadOffset, sliceSize) == payloadOffset + (1080 * sliceSize));
    }

    SECTION("from nonzero start excludes header")
    {
        auto range = SliceRange::make(540, 1080);
        REQUIRE(range.transferSize(payloadOffset, sliceSize) == 540 * sliceSize);
    }
}

TEST_CASE("ofi: SliceRange transferOffset", "[ofi][GrainSlices]")
{
    constexpr auto const payloadOffset = std::uint32_t{8192};
    constexpr auto const sliceSize = std::uint32_t{5120};

    SECTION("from start returns 0 to include header")
    {
        auto range = SliceRange::make(0, 1080);
        REQUIRE(range.transferOffset(payloadOffset, sliceSize) == 0);
    }

    SECTION("from nonzero start returns offset past header")
    {
        auto range = SliceRange::make(540, 1080);
        REQUIRE(range.transferOffset(payloadOffset, sliceSize) == payloadOffset + (540 * sliceSize));
    }
}
