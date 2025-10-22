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
    using InnerRegion = std::vector<std::uint8_t>;
    using InnerRegions = std::vector<InnerRegion>;

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

    inline std::pair<MxlRegions, InnerRegions> getHostRegionGroups()
    {
        auto innerRegions = std::vector<std::vector<std::uint8_t>>{
            std::vector<std::uint8_t>(256),
            std::vector<std::uint8_t>(512),
            std::vector<std::uint8_t>(1024),
            std::vector<std::uint8_t>(2048),
        };

        /// Warning: Do not modify the values below, you will break many tests
        // clang-format off
        auto mxlRegions =  std::vector<mxlFabricsMemoryRegion>{
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(innerRegions[0].data()),
                .size = innerRegions[0].size(),
                .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
            },
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(innerRegions[1].data()),
                .size = innerRegions[1].size(),
                .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
            },
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(innerRegions[2].data()),
                .size = innerRegions[2].size(),
                .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
            },
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(innerRegions[3].data()),
                .size = innerRegions[3].size(),
                .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
            }
        };

        // clang-format on

        return {mxlRegionsFromUser(mxlRegions.data(), mxlRegions.size()), innerRegions};
    }

    inline std::pair<mxlRegions, InnerRegions> getUserMxlRegions()
    {
        auto regions = InnerRegions{InnerRegion(256)};
        auto memoryRegions = std::vector<mxlFabricsMemoryRegion>{
            mxlFabricsMemoryRegion{.addr = reinterpret_cast<std::uintptr_t>(regions[0].data()),
                                   .size = regions[0].size(),
                                   .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0}},
        };

        mxlRegions outRegions;
        mxlFabricsRegionsFromUserBuffers(memoryRegions.data(), memoryRegions.size(), &outRegions);

        return {outRegions, regions};
    }

}
