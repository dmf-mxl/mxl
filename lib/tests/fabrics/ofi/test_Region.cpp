// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
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
    REQUIRE(group.size() == 2);

    auto const* iovecs = group.asIovec();
    REQUIRE(iovecs[0].iov_base == reinterpret_cast<void*>(0x1000));
    REQUIRE(iovecs[0].iov_len == 64);

    REQUIRE(iovecs[1].iov_base == reinterpret_cast<void*>(0x2000));
    REQUIRE(iovecs[1].iov_len == 128);
}
