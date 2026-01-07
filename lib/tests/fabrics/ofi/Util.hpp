// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
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
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    using InnerRegion = std::vector<std::uint8_t>;
    using InnerRegions = std::vector<InnerRegion>;

    [[nodiscard]]
    inline mxlFabricsTargetConfig getDefaultTargetConfig(mxlFabricsRegions regions)
    {
        auto config = mxlFabricsTargetConfig{};
        config.endpointAddress.node = "127.0.0.1";
        config.endpointAddress.service = "9090";
        config.provider = MXL_FABRICS_PROVIDER_TCP;
        config.deviceSupport = false;
        config.regions = regions;
        return config;
    }

    inline mxlFabricsInitiatorConfig getDefaultInitiatorConfig(mxlFabricsRegions regions)
    {
        auto config = mxlFabricsInitiatorConfig{};
        config.endpointAddress.node = "127.0.0.1";
        config.endpointAddress.service = "9091";
        config.provider = MXL_FABRICS_PROVIDER_TCP;
        config.deviceSupport = false;
        config.regions = regions;
        return config;
    }

    inline std::shared_ptr<Domain> getDomain(bool virtualAddress = false, bool rx_cq_data_mode = false)
    {
        auto infoList = FabricInfoList::get("127.0.0.1", "9090", Provider::TCP, FI_RMA | FI_WRITE | FI_REMOTE_WRITE, FI_EP_MSG);
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
        return MxlRegions({}, DataLayout::fromVideo({8, 0, 0, 0}));
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

        std::vector<Region> regions;
        for (auto const& innerRegion : innerRegions)
        {
            regions.emplace_back(*innerRegion.data(), innerRegion.size());
        }

        auto mxlRegions = MxlRegions(regions, DataLayout::fromVideo({8, 0, 0, 0}));
        return {mxlRegions, innerRegions};
    }

    inline MxlRegions getMxlRegions(std::vector<std::vector<std::uint8_t>> const& innerRegions,
        DataLayout dataLayout = DataLayout::fromVideo({8, 0, 0, 0}))
    {
        std::vector<Region> regions;
        for (auto const& innerRegion : innerRegions)
        {
            regions.emplace_back(*innerRegion.data(), innerRegion.size());
        }
        return {regions, dataLayout};
    }

}
