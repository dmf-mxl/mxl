// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <optional>
#include <catch2/catch_test_macros.hpp>
#include <rdma/fabric.h>
#include <mxl/fabrics.h>
#include "FabricInfo.hpp"
#include "Provider.hpp"
#include "ProviderConfig.hpp"

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
        if (provider && *provider == Provider::SHM)
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
        if (provider && *provider == Provider::TCP)
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
            if (provider && *provider == Provider::TCP && config.isSupportedFabricInfo(info))
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
            if (provider && *provider == Provider::SHM && config.isSupportedFabricInfo(info))
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
        .flags = MXL_FABRICS_IFACE_CAP_REMOTE_WRITE | MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS,
        .maxMessageSize = 65536,
    };
    auto caps = ProviderCapabilities::fromAPI(apiCaps);
    REQUIRE(caps.interfaceCaps == (MXL_FABRICS_IFACE_CAP_REMOTE_WRITE | MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS));
    REQUIRE(caps.maxMessageSize == 65536);
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
