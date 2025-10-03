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
#include "Domain.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    using InnerRegionsGroups = std::vector<std::vector<std::vector<std::uint8_t>>>;

    inline mxlTargetConfig getDefaultTargetConfig(mxlRegions regions)
    {
        mxlTargetConfig config{};
        config.endpointAddress.node = "127.0.0.1";
        config.endpointAddress.service = "9090";
        config.provider = MXL_SHARING_PROVIDER_TCP;
        config.deviceSupport = false;
        config.regions = regions;
        return config;
    }

    inline mxlInitiatorConfig getDefaultInitiatorConfig(mxlRegions regions)
    {
        mxlInitiatorConfig config{};
        config.endpointAddress.node = "127.0.0.1";
        config.endpointAddress.service = "9091";
        config.provider = MXL_SHARING_PROVIDER_TCP;
        config.deviceSupport = false;
        config.regions = regions;
        return config;
    }

    inline std::shared_ptr<Domain> getDomain(bool virtualAddress = false, bool rx_cq_data_mode = false)
    {
        auto infoList = FIInfoList::get("127.0.0.1", "9090", Provider::TCP, FI_RMA | FI_WRITE | FI_REMOTE_WRITE, FI_EP_MSG);
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

    inline std::pair<RegionGroups, InnerRegionsGroups> getHostRegionGroups()
    {
        auto innerRegions = InnerRegionsGroups{
            {
             std::vector<std::uint8_t>(256),
             std::vector<std::uint8_t>(512),
             },
            {
             std::vector<std::uint8_t>(1024),
             std::vector<std::uint8_t>(2048),
             },
        };

        auto& group0 = innerRegions[0];
        auto& group1 = innerRegions[1];

        /// Warning: Do not modify the values below, you will break many tests
        // clang-format off
        auto inputRegion0 = std::vector<mxlFabricsMemoryRegion>{
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(group0[0].data()),
                .size = group0[0].size(),
                .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
            },
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(group0[1].data()),
                .size = group0[1].size(),
                .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
            }
        };

        auto inputRegion1 = std::vector<mxlFabricsMemoryRegion>{
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(group1[0].data()),
                .size = group1[0].size(),
                .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
            },
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(group1[1].data()),
                .size = group1[1].size(),
                .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
            }
        };

        auto inputGroups = std::vector<mxlFabricsMemoryRegionGroup>{
            mxlFabricsMemoryRegionGroup{
                .regions = inputRegion0.data(),
                .count = 2,
            },
            mxlFabricsMemoryRegionGroup{
                .regions = inputRegion1.data(),
                .count = 2,
            },
        };
        // clang-format on

        return {regionGroupsfromGroups(inputGroups.data(), inputGroups.size()), innerRegions};
    }
}
