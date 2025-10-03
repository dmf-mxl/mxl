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
    auto [regions, buffers] = getHostRegionGroups();

    domain->registerRegionGroups(regions, FI_WRITE);

    auto localGroups = domain->localRegionGroups();
    REQUIRE(localGroups.size() == 2);

    auto group0 = localGroups[0];
    REQUIRE(group0.size() == 2);
    auto region00 = group0[0];
    REQUIRE(region00.addr == regions[0][0].base);
    REQUIRE(region00.len == 256);
    auto region01 = group0[1];
    REQUIRE(region01.addr == regions[0][1].base);
    REQUIRE(region01.len == 512);

    auto group1 = localGroups[1];
    REQUIRE(group1.size() == 2);
    auto region10 = group1[0];
    REQUIRE(region10.addr == regions[1][0].base);
    REQUIRE(region10.len == 1024);
    auto region11 = group1[1];
    REQUIRE(region11.addr == regions[1][1].base);
    REQUIRE(region11.len == 2048);
}

TEST_CASE("Domain - RegisteredRegion to Remote Region with Virtual Addresses", "[ofi][Domain][RemoteRegion][VirtAddr]")
{
    auto domain = getDomain(true);
    REQUIRE(domain->usingVirtualAddresses() == true);

    auto [regions, buffers] = getHostRegionGroups();

    domain->registerRegionGroups(regions, FI_WRITE);

    auto remoteGroups = domain->RemoteRegionGroups();
    REQUIRE(remoteGroups.size() == 2);

    auto group0 = remoteGroups[0];
    REQUIRE(group0.size() == 2);
    auto region00 = group0[0];
    REQUIRE(region00.addr == regions[0][0].base);
    REQUIRE(region00.len == 256);
    auto region01 = group0[1];
    REQUIRE(region01.addr == regions[0][1].base);
    REQUIRE(region01.len == 512);

    auto group1 = remoteGroups[1];
    REQUIRE(group1.size() == 2);
    auto region10 = group1[0];
    REQUIRE(region10.addr == regions[1][0].base);
    REQUIRE(region10.len == 1024);
    auto region11 = group1[1];
    REQUIRE(region11.addr == regions[1][1].base);
    REQUIRE(region11.len == 2048);
}

TEST_CASE("Domain - RegisteredRegion to Remote Region with Relative Addresses", "[ofi][Domain][RemoteRegion][RelativeAddr]")
{
    auto domain = getDomain(false);
    REQUIRE(domain->usingVirtualAddresses() == false);

    auto [regions, buffers] = getHostRegionGroups();

    domain->registerRegionGroups(regions, FI_WRITE);

    auto remoteGroups = domain->RemoteRegionGroups();
    REQUIRE(remoteGroups.size() == 2);

    auto group0 = remoteGroups[0];
    REQUIRE(group0.size() == 2);
    auto region00 = group0[0];
    REQUIRE(region00.addr == 0);
    REQUIRE(region00.len == 256);
    auto region01 = group0[1];
    REQUIRE(region01.addr == 0);
    REQUIRE(region01.len == 512);

    auto group1 = remoteGroups[1];
    REQUIRE(group1.size() == 2);
    auto region10 = group1[0];
    REQUIRE(region10.addr == 0);
    REQUIRE(region10.len == 1024);
    auto region11 = group1[1];
    REQUIRE(region11.addr == 0);
    REQUIRE(region11.len == 2048);
}

TEST_CASE("Domain - RX CQ Data Mode", "[ofi][Domain][RxCqData]")
{
    auto domain = getDomain();
    REQUIRE(domain->usingRecvBufForCqData() == false);

    auto domain2 = getDomain(false, true);
    REQUIRE(domain2->usingRecvBufForCqData() == true);
}
