// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Util.hpp
 * @brief Utility functions for MXL fabrics OFI (libfabric) unit tests
 *
 * This header provides test infrastructure for validating MXL's libfabric integration:
 *   - Default configurations for target and initiator endpoints
 *   - Domain and fabric setup with configurable attributes
 *   - Memory region creation and management
 *   - Test fixtures for TCP-based local testing
 *
 * The utilities support testing:
 *   - Memory registration (host memory, device memory)
 *   - RDMA operations (read/write)
 *   - Connection establishment (target/initiator)
 *   - Various libfabric providers (TCP, Verbs, EFA, SHM)
 *   - Virtual vs physical addressing modes
 *
 * These helpers enable comprehensive unit testing of MXL fabrics layer
 * without requiring real RDMA hardware (uses TCP loopback for most tests).
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <catch2/catch_message.hpp>
#include <fmt/format.h>
#include <rdma/fabric.h>
#include "mxl/fabrics.h"
#include "mxl/flow.h"
#include "Domain.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** @brief Alias for a single memory region (byte vector) */
    using InnerRegion = std::vector<std::uint8_t>;

    /** @brief Alias for multiple memory regions */
    using InnerRegions = std::vector<InnerRegion>;

    /**
     * @brief Create default target (receiver) configuration for testing
     *
     * Uses TCP provider on localhost:9090 for safe local testing.
     * Device support is disabled (host memory only).
     *
     * @param regions Memory regions to register for RDMA
     * @return Target configuration ready for mxlFabricsTargetCreate
     */
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

    /**
     * @brief Create default initiator (sender) configuration for testing
     *
     * Uses TCP provider on localhost:9091 for safe local testing.
     * Device support is disabled (host memory only).
     *
     * @param regions Memory regions to register for RDMA
     * @return Initiator configuration ready for mxlFabricsInitiatorCreate
     */
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

    /**
     * @brief Create a libfabric domain for testing with configurable attributes
     *
     * Sets up a TCP-based fabric domain with optional features:
     *   - Virtual addressing: Use virtual addresses for memory regions
     *   - RX CQ data mode: Enable remote CQ data support
     *
     * @param virtualAddress Enable FI_MR_VIRT_ADDR mode
     * @param rx_cq_data_mode Enable FI_RX_CQ_DATA mode
     * @return Shared pointer to configured domain
     */
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

    /**
     * @brief Create multiple host memory regions of varying sizes for testing
     *
     * Creates 4 memory regions:
     *   - Region 0: 256 bytes
     *   - Region 1: 512 bytes
     *   - Region 2: 1024 bytes
     *   - Region 3: 2048 bytes
     *
     * These sizes are specifically chosen for test validation.
     * Used to test multi-region RDMA operations and region management.
     *
     * @return Pair of (MXL region descriptors, backing storage vectors)
     * @warning Do not modify region sizes - many tests depend on these values
     */
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
                .loc = {.type = MXL_PAYLOAD_LOCATION_HOST_MEMORY, .deviceId = 0},
            },
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(innerRegions[1].data()),
                .size = innerRegions[1].size(),
                .loc = {.type = MXL_PAYLOAD_LOCATION_HOST_MEMORY, .deviceId = 0},
            },
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(innerRegions[2].data()),
                .size = innerRegions[2].size(),
                .loc = {.type = MXL_PAYLOAD_LOCATION_HOST_MEMORY, .deviceId = 0},
            },
            mxlFabricsMemoryRegion{
                .addr = reinterpret_cast<std::uintptr_t>(innerRegions[3].data()),
                .size = innerRegions[3].size(),
                .loc = {.type = MXL_PAYLOAD_LOCATION_HOST_MEMORY, .deviceId = 0},
            }
        };

        // clang-format on

        return {mxlRegionsFromUser(mxlRegions.data(), mxlRegions.size()), innerRegions};
    }

    /**
     * @brief Create a single user-provided memory region for testing
     *
     * Creates one 256-byte host memory region using the user buffer API.
     * Tests the mxlFabricsRegionsFromUserBuffers path which is used when
     * applications provide their own pre-allocated memory.
     *
     * @return Pair of (MXL region descriptor, backing storage vector)
     */
    inline std::pair<mxlRegions, InnerRegions> getUserMxlRegions()
    {
        auto regions = InnerRegions{InnerRegion(256)};
        auto memoryRegions = std::vector<mxlFabricsMemoryRegion>{
            mxlFabricsMemoryRegion{.addr = reinterpret_cast<std::uintptr_t>(regions[0].data()),
                                   .size = regions[0].size(),
                                   .loc = {.type = MXL_PAYLOAD_LOCATION_HOST_MEMORY, .deviceId = 0}},
        };

        mxlRegions outRegions;
        mxlFabricsRegionsFromUserBuffers(memoryRegions.data(), memoryRegions.size(), &outRegions);

        return {outRegions, regions};
    }

} // namespace mxl::lib::fabrics::ofi
