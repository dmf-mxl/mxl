// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <rdma/fabric.h>
#include "Util.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("Domain - RegisteredRegion to Local Region", "[ofi][Domain][LocalRegion]")
{
    auto domain = getDomain();
    auto [mxlRegions, buffers] = getHostRegionGroups();
    auto regions = mxlRegions.regions();

    domain->registerRegions(regions, FI_WRITE);

    auto localRegions = domain->localRegions();
    REQUIRE(localRegions.size() == 4);

    REQUIRE(localRegions[0].addr == regions[0].base);
    REQUIRE(localRegions[0].len == 256);
    REQUIRE(localRegions[1].addr == regions[1].base);
    REQUIRE(localRegions[1].len == 512);
    REQUIRE(localRegions[2].addr == regions[2].base);
    REQUIRE(localRegions[2].len == 1024);
    REQUIRE(localRegions[3].addr == regions[3].base);
    REQUIRE(localRegions[3].len == 2048);
}

TEST_CASE("Domain - RegisteredRegion to Remote Region with Virtual Addresses", "[ofi][Domain][RemoteRegion][VirtAddr]")
{
    auto domain = getDomain(true);
    REQUIRE(domain->usingVirtualAddresses() == true);

    auto [mxlRegions, buffers] = getHostRegionGroups();
    auto regions = mxlRegions.regions();

    domain->registerRegions(regions, FI_WRITE);

    auto remoteRegions = domain->remoteRegions();
    REQUIRE(remoteRegions.size() == 4);
    REQUIRE(remoteRegions[0].addr == regions[0].base);
    REQUIRE(remoteRegions[0].len == 256);
    REQUIRE(remoteRegions[1].addr == regions[1].base);
    REQUIRE(remoteRegions[1].len == 512);
    REQUIRE(remoteRegions[2].addr == regions[2].base);
    REQUIRE(remoteRegions[2].len == 1024);
    REQUIRE(remoteRegions[3].addr == regions[3].base);
    REQUIRE(remoteRegions[3].len == 2048);
}

TEST_CASE("Domain - RegisteredRegion to Remote Region with Relative Addresses", "[ofi][Domain][RemoteRegion][RelativeAddr]")
{
    auto domain = getDomain(false);
    REQUIRE(domain->usingVirtualAddresses() == false);

    auto [mxlRegions, buffers] = getHostRegionGroups();
    auto regions = mxlRegions.regions();

    domain->registerRegions(regions, FI_WRITE);

    auto remoteRegions = domain->remoteRegions();
    REQUIRE(remoteRegions.size() == 4);
    REQUIRE(remoteRegions[0].addr == 0);
    REQUIRE(remoteRegions[0].len == 256);
    REQUIRE(remoteRegions[1].addr == 0);
    REQUIRE(remoteRegions[1].len == 512);
    REQUIRE(remoteRegions[2].addr == 0);
    REQUIRE(remoteRegions[2].len == 1024);
    REQUIRE(remoteRegions[3].addr == 0);
    REQUIRE(remoteRegions[3].len == 2048);
}

TEST_CASE("Domain - RX CQ Data Mode", "[ofi][Domain][RxCqData]")
{
    auto domain = getDomain();
    REQUIRE(domain->usingRecvBufForCqData() == false);

    auto domain2 = getDomain(false, true);
    REQUIRE(domain2->usingRecvBufForCqData() == true);
}
