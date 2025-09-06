// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include "mxl/fabrics.h"
#include "Region.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("ofi: Region constructors", "[ofi][Constructors]")
{
    auto hostLoc = Region::Location::host();
    REQUIRE(hostLoc.isHost());
    REQUIRE(hostLoc.id() == 0);
    REQUIRE(hostLoc.iface() == FI_HMEM_SYSTEM);
    REQUIRE(hostLoc.toString() == "host");

    auto cudaLoc = Region::Location::cuda(3);
    REQUIRE_FALSE(cudaLoc.isHost());
    REQUIRE(cudaLoc.id() == 3);
    REQUIRE(cudaLoc.iface() == FI_HMEM_CUDA);
    REQUIRE(cudaLoc.toString() == "cuda, id=3");
}

TEST_CASE("ofi: RegionGroup view and iovec conversion", "[ofi][RegionGroup]")
{
    auto r1 = Region{0x1000, 64, Region::Location::host()};
    auto r2 = Region{0x2000, 128, Region::Location::host()};
    auto group = RegionGroup({r1, r2});
    REQUIRE(group.view().size() == 2);

    auto const* iovecs = group.as_iovec();
    REQUIRE(iovecs[0].iov_base == reinterpret_cast<void*>(0x1000));
    REQUIRE(iovecs[0].iov_len == 64);

    REQUIRE(iovecs[1].iov_base == reinterpret_cast<void*>(0x2000));
    REQUIRE(iovecs[1].iov_len == 128);
}

TEST_CASE("ofi: RegionGroups fromGroups and view", "[ofi][RegionGroups]")
{
    // clang-format off
    auto inputRegion = std::array<mxlFabricsMemoryRegion, 1>{
                mxlFabricsMemoryRegion{
                    .addr = 0x3000,
                    .size = 256,
                    .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
                }};

    auto inputGroups = std::array<mxlFabricsMemoryRegionGroup, 1>{
        mxlFabricsMemoryRegionGroup{
            .regions = inputRegion.data(),
            .count = 1,
        },
    };
    // clang-format on

    auto groups = RegionGroups::fromGroups(inputGroups.data(), 1);

    REQUIRE(groups.view().size() == 1);

    auto const& group = groups.view().front();
    REQUIRE(group.view().size() == 1);

    auto const& region = group.view().front();
    REQUIRE(region.base == 0x3000);
    REQUIRE(region.size == 256);
    REQUIRE(region.loc.isHost());
}
