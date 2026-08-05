// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <vector>
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
    std::uint64_t dummyGrainIndex = 0;
    std::uint16_t dummyValidSlices = 0;

    auto r1 = Region{0x1000, 64, &dummyGrainIndex, &dummyValidSlices, Region::Location::host()};
    auto r2 = Region{0x2000, 128, &dummyGrainIndex, &dummyValidSlices, Region::Location::host()};
    auto group = RegionGroup({r1, r2});
    REQUIRE(group.size() == 2);

    auto const* iovecs = group.asIovec();
    REQUIRE(iovecs[0].iov_base == reinterpret_cast<void*>(0x1000));
    REQUIRE(iovecs[0].iov_len == 64);

    REQUIRE(iovecs[1].iov_base == reinterpret_cast<void*>(0x2000));
    REQUIRE(iovecs[1].iov_len == 128);
}

TEST_CASE("ofi: split header/payload ring slot helpers", "[ofi][Region]")
{
    std::uint64_t grainIndex0 = 42;
    std::uint64_t grainIndex1 = 43;
    std::uint16_t validSlices0 = 0;
    std::uint16_t validSlices1 = 0;

    auto regions = std::vector<Region>{
        Region{0x1000, 8192, &grainIndex0, &validSlices0, Region::Location::host()},
        Region{0x2000, 1024, nullptr, nullptr, Region::Location::cuda(0)},
        Region{0x3000, 8192, &grainIndex1, &validSlices1, Region::Location::host()},
        Region{0x4000, 1024, nullptr, nullptr, Region::Location::cuda(0)},
    };

    REQUIRE(getGrainIndexInRingSlot(regions, 0, 2) == 42);
    REQUIRE(getGrainIndexInRingSlot(regions, 1, 2) == 43);

    setValidSlicesForGrain(regions, 0, 7, 2);
    setValidSlicesForGrain(regions, 1, 9, 2);
    REQUIRE(validSlices0 == 7);
    REQUIRE(validSlices1 == 9);
}
