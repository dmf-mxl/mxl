// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <optional>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <rdma/fabric.h>
#include <mxl/fabrics.h>
#include "FabricInfo.hpp"
#include "Provider.hpp"
#include "ProviderConfig.hpp"
#include "Region.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("ofi: ProviderConfig with nullopt caps produces zero libfabric caps", "[ofi][ProviderConfig]")
{
    SECTION("TCP")
    {
        auto config = ProviderConfig::tcp(false, std::nullopt);
        REQUIRE(config.getCaps() == 0);
    }
    SECTION("VERBS")
    {
        auto config = ProviderConfig::verbs(false, std::nullopt);
        REQUIRE(config.getCaps() == 0);
    }
    SECTION("EFA")
    {
        auto config = ProviderConfig::efa(false, std::nullopt);
        REQUIRE(config.getCaps() == 0);
    }
    SECTION("SHM")
    {
        auto config = ProviderConfig::shm(false, std::nullopt);
        REQUIRE(config.getCaps() == 0);
    }
}

TEST_CASE("ofi: ProviderConfig with nullopt caps accepts SHM fi_info entries", "[ofi][ProviderConfig]")
{
    auto config = ProviderConfig::shm(false, std::nullopt);

    auto accepted = false;
    for (auto info : FabricInfoList::get())
    {
        auto provider = providerFromString(info->fabric_attr->prov_name);
        if (provider && (*provider == Provider::SHM))
        {
            if (config.isSupportedFabricInfo(info))
            {
                accepted = true;
                break;
            }
        }
    }
    REQUIRE(accepted);
}

TEST_CASE("ofi: ProviderConfig with nullopt caps accepts TCP fi_info entries", "[ofi][ProviderConfig]")
{
    auto config = ProviderConfig::tcp(false, std::nullopt);

    auto accepted = false;
    for (auto info : FabricInfoList::get())
    {
        auto provider = providerFromString(info->fabric_attr->prov_name);
        if (provider && (*provider == Provider::TCP))
        {
            if (config.isSupportedFabricInfo(info))
            {
                accepted = true;
                break;
            }
        }
    }
    REQUIRE(accepted);
}

TEST_CASE("ofi: ProviderConfig with REMOTE_WRITE accepts matching fi_info entries", "[ofi][ProviderConfig]")
{
    auto caps = ProviderCapabilities{.maxMessageSize = 0, .interfaceCaps = MXL_FABRICS_IFACE_CAP_REMOTE_WRITE};

    SECTION("TCP")
    {
        auto config = ProviderConfig::tcp(false, caps);

        auto accepted = false;
        for (auto info : FabricInfoList::get())
        {
            auto provider = providerFromString(info->fabric_attr->prov_name);
            if (provider && (*provider == Provider::TCP) && config.isSupportedFabricInfo(info))
            {
                accepted = true;
                break;
            }
        }
        REQUIRE(accepted);
    }
    SECTION("SHM")
    {
        auto config = ProviderConfig::shm(false, caps);

        auto accepted = false;
        for (auto info : FabricInfoList::get())
        {
            auto provider = providerFromString(info->fabric_attr->prov_name);
            if (provider && (*provider == Provider::SHM) && config.isSupportedFabricInfo(info))
            {
                accepted = true;
                break;
            }
        }
        REQUIRE(accepted);
    }
}

TEST_CASE("ofi: ProviderCapabilities::fromAPI preserves fields", "[ofi][ProviderConfig]")
{
    auto apiCaps = mxlFabricsInterfaceCaps{
        .version = MXL_FABRICS_API_VERSION,
        .flags = MXL_FABRICS_IFACE_CAP_REMOTE_WRITE | MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS | MXL_FABRICS_IFACE_CAP_HMEM,
        .maxMessageSize = 65536,
    };
    auto caps = ProviderCapabilities::fromAPI(apiCaps);
    REQUIRE(caps.interfaceCaps ==
            (MXL_FABRICS_IFACE_CAP_REMOTE_WRITE | MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS | MXL_FABRICS_IFACE_CAP_HMEM));
    REQUIRE(caps.maxMessageSize == 65536);
}

TEST_CASE("ofi: ProviderConfig with HMEM requires FI_HMEM domains", "[ofi][ProviderConfig][hmem]")
{
    auto caps = ProviderCapabilities{.maxMessageSize = 0, .interfaceCaps = MXL_FABRICS_IFACE_CAP_REMOTE_WRITE | MXL_FABRICS_IFACE_CAP_HMEM};

    SECTION("VERBS")
    {
        auto config = ProviderConfig::verbs(true, caps);
        REQUIRE((config.getCaps() & FI_HMEM) != 0);
        REQUIRE((config.getSupportedMemoryRegistrationModes() & FI_MR_HMEM) != 0);

        auto acceptedHmem = false;
        auto acceptedNonHmem = false;
        for (auto info : FabricInfoList::get())
        {
            auto provider = providerFromString(info->fabric_attr->prov_name);
            if (!provider || (*provider != Provider::VERBS) || (info->ep_attr->type != FI_EP_MSG))
            {
                continue;
            }

            if ((info->caps & FI_HMEM) != 0)
            {
                if (config.isSupportedFabricInfo(info))
                {
                    acceptedHmem = true;
                }
            }
            else if (config.isSupportedFabricInfo(info))
            {
                acceptedNonHmem = true;
            }
        }

        // Host-only configs must keep rejecting HMEM domains; HMEM configs must accept them when present.
        REQUIRE_FALSE(acceptedNonHmem);
        if (!acceptedHmem)
        {
            WARN("No verbs FI_HMEM domain advertised by libfabric on this host; HMEM selection path could not be positively verified");
        }
    }
}

TEST_CASE("ofi: ProviderConfig without HMEM still rejects FI_HMEM domains", "[ofi][ProviderConfig][hmem]")
{
    auto caps = ProviderCapabilities{.maxMessageSize = 0, .interfaceCaps = MXL_FABRICS_IFACE_CAP_REMOTE_WRITE};
    auto config = ProviderConfig::verbs(true, caps);

    for (auto info : FabricInfoList::get())
    {
        auto provider = providerFromString(info->fabric_attr->prov_name);
        if (!provider || (*provider != Provider::VERBS))
        {
            continue;
        }
        if ((info->caps & FI_HMEM) != 0)
        {
            REQUIRE_FALSE(config.isSupportedFabricInfo(info));
        }
    }
}

TEST_CASE("ofi: regionsNeedHmem detects CUDA locations", "[ofi][Region][hmem]")
{
    std::uint64_t grainIndex = 1;
    std::uint16_t validSlices = 0;
    auto hostOnly = std::vector<Region>{
        Region{0x1000, 8192 + 64, &grainIndex, &validSlices, Region::Location::host()},
    };
    REQUIRE_FALSE(regionsNeedHmem(hostOnly));

    auto split = std::vector<Region>{
        Region{0x1000, 8192, &grainIndex, &validSlices, Region::Location::host()},
        Region{0x2000, 64, nullptr, nullptr, Region::Location::cuda(0)},
    };
    REQUIRE(regionsNeedHmem(split));
}

TEST_CASE("ofi: ProviderConfig::create dispatches to correct provider factory", "[ofi][ProviderConfig]")
{
    SECTION("TCP")
    {
        auto config = ProviderConfig::create(Provider::TCP, false, std::nullopt);
        REQUIRE(config.getProviderName() == "tcp");
        REQUIRE(config.getEndpointType() == FI_EP_MSG);
    }
    SECTION("VERBS")
    {
        auto config = ProviderConfig::create(Provider::VERBS, false, std::nullopt);
        REQUIRE(config.getProviderName() == "verbs");
        REQUIRE(config.getEndpointType() == FI_EP_MSG);
    }
    SECTION("EFA")
    {
        auto config = ProviderConfig::create(Provider::EFA, false, std::nullopt);
        REQUIRE(config.getProviderName() == "efa");
        REQUIRE(config.getEndpointType() == FI_EP_RDM);
    }
    SECTION("SHM")
    {
        auto config = ProviderConfig::create(Provider::SHM, false, std::nullopt);
        REQUIRE(config.getProviderName() == "shm");
        REQUIRE(config.getEndpointType() == FI_EP_RDM);
    }
}
