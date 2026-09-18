// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <array>
#include <limits>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <rdma/fabric.h>
#include "Exception.hpp"
#include "Util.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("ofi: Domain - RegisteredRegion to Local Region", "[ofi][Domain][LocalRegion]")
{
    auto domain = getDomain();
    auto [mxlFabricsRegions, buffers] = getHostRegionGroups();
    auto regions = mxlFabricsRegions.regions();

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

TEST_CASE("ofi: Domain - RegisteredRegion to Remote Region with Virtual Addresses", "[ofi][Domain][RemoteRegion][VirtAddr]")
{
    auto domain = getDomain(true);
    REQUIRE(domain->usingVirtualAddresses() == true);

    auto [mxlFabricsRegions, buffers] = getHostRegionGroups();
    auto regions = mxlFabricsRegions.regions();

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

    auto [mxlFabricsRegions, buffers] = getHostRegionGroups();
    auto regions = mxlFabricsRegions.regions();

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

// When the regions handed to the domain are contiguous in host memory (as they
// are for a flow that uses the contiguous grain pool), they must be collapsed
// into a single memory registration instead of one registration per region.
TEST_CASE("ofi: Domain - contiguous regions are coalesced into a single registration", "[ofi][Domain][Coalesce]")
{
    // One backing buffer; four sub-regions laid out back to back.
    constexpr auto sizes = std::array<std::size_t, 4>{256, 512, 1024, 2048};
    auto buffer = std::vector<std::uint8_t>(sizes[0] + sizes[1] + sizes[2] + sizes[3]);
    auto const base = reinterpret_cast<std::uintptr_t>(buffer.data());

    auto regions = std::vector<Region>{};
    auto offset = std::uintptr_t{0};
    for (auto const size : sizes)
    {
        regions.emplace_back(base + offset, size, nullptr, nullptr);
        offset += size;
    }

    SECTION("relative addressing exposes per-region offsets within the single registration")
    {
        auto domain = getDomain(false);
        REQUIRE(domain->usingVirtualAddresses() == false);

        domain->registerRegions(regions, FI_WRITE);

        auto const remote = domain->remoteRegions();
        REQUIRE(remote.size() == 4);

        // With a single shared registration, each region's relative address is
        // its cumulative offset. (Per-region registration would yield 0 for all,
        // as exercised by the non-contiguous tests above.)
        REQUIRE(remote[0].addr == 0);
        REQUIRE(remote[1].addr == sizes[0]);
        REQUIRE(remote[2].addr == sizes[0] + sizes[1]);
        REQUIRE(remote[3].addr == sizes[0] + sizes[1] + sizes[2]);

        REQUIRE(remote[0].len == sizes[0]);
        REQUIRE(remote[3].len == sizes[3]);

        // All regions share one rkey, confirming a single memory registration.
        REQUIRE(remote[1].rkey == remote[0].rkey);
        REQUIRE(remote[2].rkey == remote[0].rkey);
        REQUIRE(remote[3].rkey == remote[0].rkey);
    }

    SECTION("virtual addressing exposes each region's own address from the single registration")
    {
        auto domain = getDomain(true);
        REQUIRE(domain->usingVirtualAddresses() == true);

        domain->registerRegions(regions, FI_WRITE);

        auto const remote = domain->remoteRegions();
        REQUIRE(remote.size() == 4);

        REQUIRE(remote[0].addr == regions[0].base);
        REQUIRE(remote[1].addr == regions[1].base);
        REQUIRE(remote[2].addr == regions[2].base);
        REQUIRE(remote[3].addr == regions[3].base);

        REQUIRE(remote[1].rkey == remote[0].rkey);
        REQUIRE(remote[3].rkey == remote[0].rkey);
    }

    SECTION("local regions keep each region's own address")
    {
        auto domain = getDomain();

        domain->registerRegions(regions, FI_WRITE);

        auto const local = domain->localRegions();
        REQUIRE(local.size() == 4);
        REQUIRE(local[0].addr == regions[0].base);
        REQUIRE(local[1].addr == regions[1].base);
        REQUIRE(local[2].addr == regions[2].base);
        REQUIRE(local[3].addr == regions[3].base);
    }
}

TEST_CASE("ofi: Domain - padded contiguous regions use one registration", "[ofi][Domain][Coalesce]")
{
    constexpr auto logicalSize = std::size_t{9216};
    constexpr auto registrationSize = std::size_t{12288};
    constexpr auto regionCount = std::size_t{3};
    auto buffer = std::vector<std::uint8_t>(registrationSize * regionCount);
    auto const base = reinterpret_cast<std::uintptr_t>(buffer.data());

    auto regions = std::vector<Region>{};
    for (auto i = std::size_t{0}; i < regionCount; ++i)
    {
        regions.emplace_back(base + (i * registrationSize), logicalSize, nullptr, nullptr, Region::Location::host(), registrationSize);
    }

    auto domain = getDomain(false);
    domain->registerRegions(regions, FI_WRITE);

    auto const remote = domain->remoteRegions();
    REQUIRE(remote.size() == regionCount);
    REQUIRE(remote[0].addr == 0U);
    REQUIRE(remote[1].addr == registrationSize);
    REQUIRE(remote[2].addr == 2U * registrationSize);
    REQUIRE(remote[0].len == logicalSize);
    REQUIRE(remote[1].len == logicalSize);
    REQUIRE(remote[2].len == logicalSize);
    REQUIRE(remote[1].rkey == remote[0].rkey);
    REQUIRE(remote[2].rkey == remote[0].rkey);
}

TEST_CASE("ofi: Domain - invalid registration spans are rejected", "[ofi][Domain]")
{
    auto domain = getDomain();

    SECTION("registration span is smaller than the logical region")
    {
        auto const region = Region{0x1000U, 128U, nullptr, nullptr, Region::Location::host(), 64U};
        REQUIRE_THROWS_AS(domain->registerRegion(region, FI_WRITE), Exception);
    }

    SECTION("registration span exceeds the address space")
    {
        auto const base = std::numeric_limits<std::uintptr_t>::max() - 31U;
        auto const region = Region{base, 64U, nullptr, nullptr};
        REQUIRE_THROWS_AS(domain->registerRegion(region, FI_WRITE), Exception);
    }

    SECTION("coalesced input is validated before registration")
    {
        auto const regions = std::vector<Region>{
            Region{0x1000U, 128U, nullptr, nullptr, Region::Location::host(), 64U},
            Region{0x1040U, 64U, nullptr, nullptr},
        };
        REQUIRE_THROWS_AS(domain->registerRegions(regions, FI_WRITE), Exception);
    }
}
