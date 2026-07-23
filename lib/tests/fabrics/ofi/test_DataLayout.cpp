// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include "DataLayout.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("ofi: DataLayout::Discrete activePlaneCount", "[ofi][DataLayout]")
{
    SECTION("all zeros yields 0 planes")
    {
        auto layout = DataLayout::Discrete{
            .sliceSizes = {0, 0, 0, 0},
              .totalSlices = 0
        };
        REQUIRE(layout.activePlaneCount() == 0);
    }

    SECTION("single plane (v210)")
    {
        auto layout = DataLayout::Discrete{
            .sliceSizes = {5120, 0, 0, 0},
              .totalSlices = 1080
        };
        REQUIRE(layout.activePlaneCount() == 1);
    }

    SECTION("two planes (v210a)")
    {
        auto layout = DataLayout::Discrete{
            .sliceSizes = {5120, 2560, 0, 0},
              .totalSlices = 1080
        };
        REQUIRE(layout.activePlaneCount() == 2);
    }

    SECTION("four planes")
    {
        auto layout = DataLayout::Discrete{
            .sliceSizes = {100, 200, 300, 400},
              .totalSlices = 10
        };
        REQUIRE(layout.activePlaneCount() == 4);
    }
}

TEST_CASE("ofi: DataLayout::Discrete planePayloadOffset", "[ofi][DataLayout]")
{
    constexpr auto headerSize = std::uint32_t{8192};

    SECTION("single plane: plane 0 starts at the grain payload offset")
    {
        auto layout = DataLayout::Discrete{
            .sliceSizes = {5120, 0, 0, 0},
              .totalSlices = 1080
        };
        REQUIRE(layout.planePayloadOffset(0, headerSize) == headerSize);
    }

    SECTION("two planes: plane 0 at header, plane 1 after fill data")
    {
        constexpr auto height = std::uint16_t{1080};
        constexpr auto fillSlice = std::uint32_t{5120};
        constexpr auto keySlice = std::uint32_t{2560};

        auto layout = DataLayout::Discrete{
            .sliceSizes = {fillSlice, keySlice, 0, 0},
              .totalSlices = height
        };

        REQUIRE(layout.planePayloadOffset(0, headerSize) == headerSize);
        REQUIRE(layout.planePayloadOffset(1, headerSize) == headerSize + (height * fillSlice));
    }

    SECTION("four planes: each plane offset accumulates")
    {
        constexpr auto slices = std::uint32_t{10};
        auto layout = DataLayout::Discrete{
            .sliceSizes = {100, 200, 300, 400},
              .totalSlices = slices
        };

        REQUIRE(layout.planePayloadOffset(0, headerSize) == headerSize);
        REQUIRE(layout.planePayloadOffset(1, headerSize) == headerSize + (slices * 100));
        REQUIRE(layout.planePayloadOffset(2, headerSize) == headerSize + (slices * 100) + (slices * 200));
        REQUIRE(layout.planePayloadOffset(3, headerSize) == headerSize + (slices * 100) + (slices * 200) + (slices * 300));
    }
}

TEST_CASE("ofi: DataLayout fromDiscrete factory", "[ofi][DataLayout]")
{
    auto sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{5120, 2560, 0, 0};
    auto layout = DataLayout::fromDiscrete(sliceSizes, 1080);

    REQUIRE(layout.isDiscrete());
    REQUIRE_FALSE(layout.isContinuous());

    auto const& discrete = layout.asDiscrete();
    REQUIRE(discrete.sliceSizes == sliceSizes);
    REQUIRE(discrete.totalSlices == 1080);
    REQUIRE(discrete.activePlaneCount() == 2);
}
