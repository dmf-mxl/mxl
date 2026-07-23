// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <catch2/catch_message.hpp>
#include <fmt/format.h>
#include <rdma/fabric.h>
#include "mxl/fabrics.h"
#include "DataLayout.hpp"
#include "Domain.hpp"
#include "FabricAddress.hpp"
#include "ProviderConfig.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    using InnerRegion = std::vector<std::uint8_t>;
    using InnerRegions = std::vector<InnerRegion>;

    [[nodiscard]]
    inline mxlFabricsTargetConfig getDefaultTargetConfig(mxlFlowWriter writer)
    {
        auto config = mxlFabricsTargetConfig{};
        config.interface.address.node = "127.0.0.1";
        config.interface.address.service = "9090";
        config.interface.provider = MXL_FABRICS_PROVIDER_TCP;
        config.writer = writer;
        return config;
    }

    inline mxlFabricsInitiatorConfig getDefaultInitiatorConfig(mxlFlowReader reader)
    {
        auto config = mxlFabricsInitiatorConfig{};
        config.interface.address.node = "127.0.0.1";
        config.interface.address.service = "9091";
        config.interface.provider = MXL_FABRICS_PROVIDER_TCP;
        config.reader = reader;
        return config;
    }

    inline std::shared_ptr<Domain> getDomain(bool virtualAddress = false, bool rx_cq_data_mode = false)
    {
        auto providerConfig = ProviderConfig::tcp(false, std::nullopt);
        auto fabricAddress = FabricAddress::parse(Provider::TCP, "127.0.0.1", "1111");

        auto infoList = FabricInfoList::getSourceInterfaces(providerConfig, fabricAddress);
        auto info = *infoList.begin();

        auto fabric = Fabric::open(info);
        auto domain = Domain::open(fabric);

        if (virtualAddress)
        {
            fabric->info()->domain_attr->mr_mode |= FI_MR_VIRT_ADDR;
        }
        else
        {
            fabric->info()->domain_attr->mr_mode &= ~FI_MR_VIRT_ADDR;
        }

        if (rx_cq_data_mode)
        {
            fabric->info()->rx_attr->mode |= FI_RX_CQ_DATA;
        }
        else
        {
            fabric->info()->rx_attr->mode &= ~FI_RX_CQ_DATA;
        }

        return domain;
    }

    inline MxlRegions getEmptyVideoMxlRegions()
    {
        return MxlRegions({}, DataLayout::fromDiscrete({8, 0, 0, 0}, 1));
    }

    inline std::pair<MxlRegions, InnerRegions> getHostRegionGroups()
    {
        /// Warning: Do not modify the values below, you will break many tests
        auto innerRegions = std::vector<std::vector<std::uint8_t>>{
            std::vector<std::uint8_t>(256),
            std::vector<std::uint8_t>(512),
            std::vector<std::uint8_t>(1024),
            std::vector<std::uint8_t>(2048),
        };

        auto regions = std::vector<Region>{};
        regions.reserve(innerRegions.size());
        for (auto const& innerRegion : innerRegions)
        {
            regions.emplace_back(*innerRegion.data(), innerRegion.size(), nullptr, nullptr);
        }

        auto mxlRegions = MxlRegions(regions, DataLayout::fromDiscrete({8, 0, 0, 0}, 1));
        return {mxlRegions, innerRegions};
    }

    inline MxlRegions getMxlRegions(std::vector<std::vector<std::uint8_t>> const& innerRegions,
        DataLayout dataLayout = DataLayout::fromDiscrete({8, 0, 0, 0}, 1))
    {
        auto regions = std::vector<Region>{};
        regions.reserve(innerRegions.size());
        for (auto const& innerRegion : innerRegions)
        {
            regions.emplace_back(*innerRegion.data(), innerRegion.size(), nullptr, nullptr);
        }
        return {regions, dataLayout};
    }

}
