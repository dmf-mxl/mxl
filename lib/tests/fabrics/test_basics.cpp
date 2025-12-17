// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <uuid.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <picojson/picojson.h>
#include <rdma/fabric.h>
#include <mxl/fabrics.h>
#include "mxl-internal/Logging.hpp"
#include "mxl/dataformat.h"
#include "mxl/flow.h"
#include "mxl/mxl.h"
#include "../Utils.hpp"

#ifdef MXL_FABRICS_OFI
// clang-format off
    #include "TargetInfo.hpp"
// clang-format on
#else
#endif

TEST_CASE("Fabrics basic creation/destroy", "[fabrics][basics]")
{
    // Create an empty region, because management tests does not need to do transfers
    auto regionsConfig = mxlFabricsExtRegionsConfig{.regions = nullptr, .regionsCount = 0, .sliceSize = {}, .format = MXL_DATA_FORMAT_UNSPECIFIED};
    mxlFabricsRegions regions;
    mxlFabricsExtGetRegions(&regionsConfig, &regions);

    auto instance = mxlCreateInstance("/dev/shm/", "");

    mxlFabricsInstance fabrics;
    SECTION("instance creation/destruction")
    {
        REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

        SECTION("target creation/destruction")
        {
            mxlFabricsTarget target;
            REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
            REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
        }

        SECTION("initiator creation/destruction")
        {
            mxlFabricsInitiator initiator;
            REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
            REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
        }

        REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    }
}

TEST_CASE("Fabrics connection oriented activation tests", "[fabrics][connected][activation]")
{
    // Create an empty region, because management tests does not need to do transfers
    auto regionsConfig = mxlFabricsExtRegionsConfig{.regions = nullptr, .regionsCount = 0, .sliceSize = {}, .format = MXL_DATA_FORMAT_VIDEO};
    mxlFabricsRegions regions;
    mxlFabricsExtGetRegions(&regionsConfig, &regions);

    auto instance = mxlCreateInstance("/dev/shm/", "");

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    SECTION("target/initiator setup")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .regions = regions,
            .deviceSupport = false
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .regions = regions,
            .deviceSupport = false
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);

        SECTION("initiator add/remove target")
        {
            REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

            SECTION("non-blocking")
            {
                // try to connect them for 5 seconds
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                std::uint16_t dummyIndex;
                std::uint16_t dummyValidSlices;
                do
                {
                    mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // make progress on target

                    auto status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                    if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
                    {
                        FAIL("Something went wrong in the initiator: " + std::to_string(status));
                    }

                    if (status == MXL_STATUS_OK)
                    {
                        // connected
                        return;
                    }
                }
                while (std::chrono::steady_clock::now() < deadline);

                FAIL("Failed to connect in 5 seconds");
            }

            SECTION("blocking")
            {
                // try to connect them for 5 seconds
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                std::uint16_t dummyIndex;
                std::uint16_t dummyValidSlices;
                do
                {
                    mxlFabricsTargetReadGrain(
                        target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count()); // make progress on target

                    auto status = mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                    if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
                    {
                        FAIL("Something went wrong in the initiator: " + std::to_string(status));
                    }

                    if (status == MXL_STATUS_OK)
                    {
                        // connected
                        return;
                    }
                }
                while (std::chrono::steady_clock::now() < deadline);

                FAIL("Failed to connect in 5 seconds");
            }

            REQUIRE(mxlFabricsInitiatorRemoveTarget(initiator, targetInfo) == MXL_STATUS_OK);
        }

        REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
    }

    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE("Fabrics connectionless activation tests", "[fabrics][connectionless][activation]")
{
    // Create an empty region, because management tests does not need to do transfers
    auto regionsConfig = mxlFabricsExtRegionsConfig{.regions = nullptr, .regionsCount = 0, .sliceSize = {}, .format = MXL_DATA_FORMAT_VIDEO};
    mxlFabricsRegions regions;
    mxlFabricsExtGetRegions(&regionsConfig, &regions);

    auto instance = mxlCreateInstance("/dev/shm/", "");

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    auto targetConfig = mxlFabricsTargetConfig{
        .endpointAddress = mxlFabricsEndpointAddress{.node = "target", .service = "activation"},
        .provider = MXL_FABRICS_PROVIDER_SHM,
        .regions = regions,
        .deviceSupport = false
    };
    mxlFabricsTargetInfo targetInfo;
    SECTION("target setup")
    {
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
    }
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

    auto initiatorConfig = mxlFabricsInitiatorConfig{
        .endpointAddress = mxlFabricsEndpointAddress{.node = "initiator", .service = "activation"},
        .provider = MXL_FABRICS_PROVIDER_SHM,
        .regions = regions,
        .deviceSupport = false
    };
    SECTION("initiator setup")
    {
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
    }
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);

    SECTION("initiator add/remove target")
    {
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorRemoveTarget(initiator, targetInfo) == MXL_STATUS_OK);
    }
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    SECTION("non-blocking")
    {
        // try to connect them for 5 seconds
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::uint16_t dummyIndex;
        std::uint16_t dummyValidSlices;
        do
        {
            mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // make progress on target

            auto status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
            if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
            {
                FAIL("Something went wrong in the initiator: " + std::to_string(status));
            }

            if (status == MXL_STATUS_OK)
            {
                // connected
                return;
            }
        }
        while (std::chrono::steady_clock::now() < deadline);

        FAIL("Failed to connect in 5 seconds");
    }

    SECTION("blocking")
    {
        // try to connect them for 5 seconds
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::uint16_t dummyIndex;
        std::uint16_t dummyValidSlices;
        do
        {
            mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count()); // make progress on target

            auto status = mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
            if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
            {
                FAIL("Something went wrong in the initiator: " + std::to_string(status));
            }

            if (status == MXL_STATUS_OK)
            {
                // connected
                return;
            }
        }
        while (std::chrono::steady_clock::now() < deadline);

        FAIL("Failed to connect in 5 seconds");
    }

    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE("Fabrics: transfer grain with user buffers", "[fabrics][transfer][user]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance("/dev/shm/", "");

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    // Setup target regions
    mxlFabricsRegions mxlTargetRegions;
    auto targetRegion = std::vector<std::uint8_t>(1e6);
    auto targetRegions = std::vector<mxlFabricsExtMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(targetRegion.data()),
         .size = targetRegion.size(),
         .loc = {.type = MXL_PAYLOAD_LOCATION_HOST_MEMORY, .deviceId = 0},
         }
    };
    auto config = mxlFabricsExtRegionsConfig{
        .regions = targetRegions.data(), .regionsCount = targetRegions.size(), .sliceSize = {720}, .format = MXL_DATA_FORMAT_VIDEO};
    mxlFabricsExtGetRegions(&config, &mxlTargetRegions);

    mxlFabricsRegions mxlInitiatorRegions;
    auto initiatorRegion = std::vector<std::uint8_t>(1e6);
    auto initiatorRegions = std::vector<mxlFabricsExtMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(initiatorRegion.data()),
         .size = initiatorRegion.size(),
         .loc = {.type = MXL_PAYLOAD_LOCATION_HOST_MEMORY, .deviceId = 0},
         }
    };
    mxlFabricsExtGetRegions(&config, &mxlInitiatorRegions);

    SECTION("RC")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .regions = mxlTargetRegions,
            .deviceSupport = false
        };

        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

        // Setup initiator regions

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .regions = mxlInitiatorRegions,
            .deviceSupport = false
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

        // try to connect them for 5 seconds
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::uint16_t dummyIndex;
        std::uint16_t dummyValidSlices;
        do
        {
            mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // make progress on target

            auto status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
            if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
            {
                FAIL("Something went wrong in the initiator: " + std::to_string(status));
            }

            if (status == MXL_STATUS_OK)
            {
                // connected
                break;
            }
            if (std::chrono::steady_clock::now() > deadline)
            {
                FAIL("Failed to connect in 5 seconds");
            }
        }
        while (true);

        SECTION("non-blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // target make progress
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                auto status = mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices);
                if (status == MXL_ERR_INTERRUPTED)
                {
                    FAIL("Peer disconnected before the transfer completed");
                }
                if (status == MXL_STATUS_OK)
                {
                    // transfer complete
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        SECTION("blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count()); // target make progress
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                auto status = mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count());
                if (status == MXL_ERR_INTERRUPTED)
                {
                    FAIL("Peer disconnected before the transfer completed");
                }
                if (status == MXL_STATUS_OK)
                {
                    // transfer complete
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        mxlFabricsFreeTargetInfo(targetInfo);
    }

    SECTION("RDM")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "target", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .regions = mxlTargetRegions,
            .deviceSupport = false
        };

        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

        // Setup initiator regions

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "initiator", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .regions = mxlInitiatorRegions,
            .deviceSupport = false
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

        // try to connect them for 5 seconds
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::uint16_t dummyIndex;
        std::uint16_t dummyValidSlices;
        do
        {
            mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // make progress on target

            auto status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
            if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
            {
                FAIL("Something went wrong in the initiator: " + std::to_string(status));
            }

            if (status == MXL_STATUS_OK)
            {
                // connected
                break;
            }
            if (std::chrono::steady_clock::now() > deadline)
            {
                FAIL("Failed to connect in 5 seconds");
            }
        }
        while (true);

        SECTION("non-blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // target make progress
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                auto status = mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices);
                if (status == MXL_ERR_INTERRUPTED)
                {
                    FAIL("Peer disconnected before the transfer completed");
                }
                if (status == MXL_STATUS_OK)
                {
                    // transfer complete
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        SECTION("blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count()); // target make progress
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                auto status = mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count());
                if (status == MXL_ERR_INTERRUPTED)
                {
                    FAIL("Peer disconnected before the transfer completed");
                }
                if (status == MXL_STATUS_OK)
                {
                    // transfer complete
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        mxlFabricsFreeTargetInfo(targetInfo);
    }

    REQUIRE(mxlFabricsRegionsFree(mxlInitiatorRegions) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsRegionsFree(mxlTargetRegions) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Transfer Grain with flows", "[Fabrics][Transfer][Flows]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance(domain.c_str(), "");

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    mxlFlowConfigInfo configInfo;
    REQUIRE(mxlCreateFlow(instance, flowDef.c_str(), "", &configInfo) == MXL_STATUS_OK);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowId, "", &writer) == MXL_STATUS_OK);

    mxlFabricsRegions mxlTargetRegions;
    REQUIRE(mxlFabricsRegionsForFlowWriter(writer, &mxlTargetRegions) == MXL_STATUS_OK);

    // Initiator
    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    // Setup initiator regions
    mxlFabricsRegions mxlInitiatorRegions;
    REQUIRE(mxlFabricsRegionsForFlowReader(reader, &mxlInitiatorRegions) == MXL_STATUS_OK);

    SECTION("RC")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .regions = mxlTargetRegions,
            .deviceSupport = false
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .regions = mxlInitiatorRegions,
            .deviceSupport = false
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

        // try to connect them for 5 seconds
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::uint16_t dummyIndex;
        std::uint16_t dummyValidSlices;
        do
        {
            mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // make progress on target

            auto status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
            if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
            {
                FAIL("Something went wrong in the initiator: " + std::to_string(status));
            }

            if (status == MXL_STATUS_OK)
            {
                // connected
                break;
            }
            if (std::chrono::steady_clock::now() > deadline)
            {
                FAIL("Failed to connect in 5 seconds");
            }
        }
        while (true);

        SECTION("non-blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // target make progress
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                auto status = mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices);
                if (status == MXL_ERR_INTERRUPTED)
                {
                    FAIL("Peer disconnected before the transfer completed");
                }
                if (status == MXL_STATUS_OK)
                {
                    // transfer complete
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        SECTION("blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count()); // target make progress
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                auto status = mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count());
                if (status == MXL_ERR_INTERRUPTED)
                {
                    FAIL("Peer disconnected before the transfer completed");
                }
                if (status == MXL_STATUS_OK)
                {
                    // transfer complete
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        mxlFabricsFreeTargetInfo(targetInfo);
    }

    SECTION("RDM")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "target", .service = "test"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .regions = mxlTargetRegions,
            .deviceSupport = false
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "initiator", .service = "test"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .regions = mxlInitiatorRegions,
            .deviceSupport = false
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

        // try to connect them for 5 seconds
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::uint16_t dummyIndex;
        std::uint16_t dummyValidSlices;
        do
        {
            mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // make progress on target

            auto status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
            if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
            {
                FAIL("Something went wrong in the initiator: " + std::to_string(status));
            }

            if (status == MXL_STATUS_OK)
            {
                // connected
                break;
            }
            if (std::chrono::steady_clock::now() > deadline)
            {
                FAIL("Failed to connect in 5 seconds");
            }
        }
        while (true);

        SECTION("non-blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // target make progress
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                auto status = mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices);
                if (status == MXL_ERR_INTERRUPTED)
                {
                    FAIL("Peer disconnected before the transfer completed");
                }
                if (status == MXL_STATUS_OK)
                {
                    // transfer complete
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        SECTION("blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count()); // target make progress
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                auto status = mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count());
                if (status == MXL_ERR_INTERRUPTED)
                {
                    FAIL("Peer disconnected before the transfer completed");
                }
                if (status == MXL_STATUS_OK)
                {
                    // transfer complete
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        mxlFabricsFreeTargetInfo(targetInfo);
    }

    REQUIRE(mxlFabricsRegionsFree(mxlInitiatorRegions) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsRegionsFree(mxlTargetRegions) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyFlow(instance, flowId) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Transfer Grain with flows multi target",
    "[Fabrics][Transfer][Flows][Multi-targets]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    auto jsonValue = picojson::value{};
    REQUIRE(picojson::parse(jsonValue, flowDef).empty());
    REQUIRE(jsonValue.is<picojson::object>());
    auto root = jsonValue.get<picojson::object>();

    auto instance = mxlCreateInstance(domain.c_str(), "");

    // Initiator
    mxlFlowReader reader;
    mxlFlowConfigInfo configInfoDummy;
    REQUIRE(mxlCreateFlow(instance, flowDef.c_str(), "{}", &configInfoDummy) == MXL_STATUS_OK);
    REQUIRE(mxlCreateFlowReader(instance, root.at("id").get<std::string>().c_str(), "", &reader) == MXL_STATUS_OK);

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
    // Setup initiator regions
    mxlFabricsRegions mxlInitiatorRegions;
    REQUIRE(mxlFabricsRegionsForFlowReader(reader, &mxlInitiatorRegions) == MXL_STATUS_OK);

    constexpr auto nbTargets = 2;
    std::array<mxlFabricsTarget, nbTargets> targets;
    std::array<std::string, nbTargets> flowIds;
    std::array<std::string, nbTargets> flowDefs;
    std::array<mxlFlowConfigInfo, nbTargets> configInfo;
    std::array<mxlFlowWriter, nbTargets> writer;
    std::array<mxlFabricsRegions, nbTargets> mxlTargetRegions;
    for (size_t i = 0; i < nbTargets; i++)
    {
        REQUIRE(mxlFabricsCreateTarget(fabrics, &targets[i]) == MXL_STATUS_OK);
        flowIds[i] = uuids::to_string(uuids::uuid_system_generator{}());
        root.at("id") = picojson::value{flowIds[i]};
        flowDefs[i] = picojson::value{root}.serialize();
        REQUIRE(mxlCreateFlow(instance, flowDefs[i].c_str(), "", &configInfo[i]) == MXL_STATUS_OK);
        REQUIRE(mxlCreateFlowWriter(instance, flowIds[i].c_str(), "", &writer[i]) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsRegionsForFlowWriter(writer[i], &mxlTargetRegions[i]) == MXL_STATUS_OK);
    }

    SECTION("RC")
    {
        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .regions = mxlInitiatorRegions,
            .deviceSupport = false
        };

        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);

        std::array<mxlFabricsTargetConfig, nbTargets> targetConfig;
        std::array<mxlFabricsTargetInfo, nbTargets> targetInfo;
        for (size_t i = 0; i < nbTargets; i++)
        {
            targetConfig[i] = mxlFabricsTargetConfig{
                .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
                .provider = MXL_FABRICS_PROVIDER_TCP,
                .regions = mxlTargetRegions[i],
                .deviceSupport = false
            };
            REQUIRE(mxlFabricsTargetSetup(targets[i], &targetConfig[i], &targetInfo[i]) == MXL_STATUS_OK);

            REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo[i]) == MXL_STATUS_OK);
        }

        // try to connect them for 5 seconds
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::uint16_t dummyIndex;
        std::uint16_t dummyValidSlices;
        bool initiatorConnected = false;
        do
        {
            for (auto& target : targets)
            {
                auto status = mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // make progress on target
                if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
                {
                    FAIL("Something went wrong in the target: " + std::to_string(status));
                }
            }

            auto status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
            if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
            {
                FAIL("Something went wrong in the initiator: " + std::to_string(status));
            }

            if (status == MXL_STATUS_OK)
            {
                initiatorConnected = true;
            }
            if (std::chrono::steady_clock::now() > deadline)
            {
                FAIL("Failed to connect in 5 seconds");
            }

            if (initiatorConnected)
            {
                // all connected
                break;
            }
        }
        while (true);

        SECTION("non-blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                for (auto& target : targets)
                {
                    mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // target make progress
                }
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            std::array<bool, nbTargets> transferComplete = {};
            do
            {
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                for (size_t i = 0; i < nbTargets; i++)
                {
                    auto status = mxlFabricsTargetReadGrainNonBlocking(targets[i], &dummyIndex, &dummyValidSlices);
                    if (status == MXL_ERR_INTERRUPTED)
                    {
                        FAIL("Peer disconnected before the transfer completed");
                    }
                    if (status == MXL_STATUS_OK)
                    {
                        // transfer complete
                        transferComplete[i] = true;
                    }
                }

                if (std::ranges::all_of(transferComplete, [](bool v) { return v; }))
                {
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        SECTION("blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                for (auto& target : targets)
                {
                    mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count()); // target make progress
                }
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            std::array<bool, nbTargets> transferComplete = {};
            do
            {
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                for (size_t i = 0; i < nbTargets; i++)
                {
                    auto status = mxlFabricsTargetReadGrain(targets[i], &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count());
                    if (status == MXL_ERR_INTERRUPTED)
                    {
                        FAIL("Peer disconnected before the transfer completed");
                    }
                    if (status == MXL_STATUS_OK)
                    {
                        // transfer complete
                        transferComplete[i] = true;
                    }
                }
                if (std::ranges::all_of(transferComplete, [](bool v) { return v; }))
                {
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        for (size_t i = 0; i < nbTargets; i++)
        {
            mxlFabricsFreeTargetInfo(targetInfo[i]);
        }
    }

    SECTION("RDM")
    {
        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .endpointAddress = mxlFabricsEndpointAddress{.node = "initiator", .service = "test"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .regions = mxlInitiatorRegions,
            .deviceSupport = false
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);

        std::array<mxlFabricsTargetConfig, nbTargets> targetConfig;
        std::array<mxlFabricsTargetInfo, nbTargets> targetInfo;
        for (size_t i = 0; i < nbTargets; i++)
        {
            targetConfig[i] = mxlFabricsTargetConfig{
                .endpointAddress = mxlFabricsEndpointAddress{.node = "target", .service = "test"},
                .provider = MXL_FABRICS_PROVIDER_SHM,
                .regions = mxlTargetRegions[i],
                .deviceSupport = false
            };
            REQUIRE(mxlFabricsTargetSetup(targets[i], &targetConfig[i], &targetInfo[i]) == MXL_STATUS_OK);

            REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo[i]) == MXL_STATUS_OK);
        }

        // try to connect them for 5 seconds
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::uint16_t dummyIndex;
        std::uint16_t dummyValidSlices;
        do
        {
            for (auto& target : targets)
            {
                auto status = mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // make progress on target
                if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
                {
                    FAIL("Something went wrong in the target: " + std::to_string(status));
                }
            }

            auto status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
            if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
            {
                FAIL("Something went wrong in the initiator: " + std::to_string(status));
            }

            if (status == MXL_STATUS_OK)
            {
                // connected
                break;
            }
            if (std::chrono::steady_clock::now() > deadline)
            {
                FAIL("Failed to connect in 5 seconds");
            }
        }
        while (true);

        SECTION("non-blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                for (auto& target : targets)
                {
                    mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex, &dummyValidSlices); // target make progress
                }

                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            std::array<bool, nbTargets> transferComplete = {};
            do
            {
                mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
                for (size_t i = 0; i < nbTargets; i++)
                {
                    auto status = mxlFabricsTargetReadGrainNonBlocking(targets[i], &dummyIndex, &dummyValidSlices);
                    if (status == MXL_ERR_INTERRUPTED)
                    {
                        FAIL("Peer disconnected before the transfer completed");
                    }
                    if (status == MXL_STATUS_OK)
                    {
                        // transfer complete
                        transferComplete[i] = true;
                    }
                }
                if (std::ranges::all_of(transferComplete, [](bool v) { return v; }))
                {
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        SECTION("blocking")
        {
            // try to post a transfer within 5 seconds
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do
            {
                for (auto& target : targets)
                {
                    mxlFabricsTargetReadGrain(target, &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count()); // target make progress
                }
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                if (mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1) == MXL_STATUS_OK)
                {
                    // transfer started
                    break;
                }

                if (std::chrono::steady_clock::now() > deadline)
                {
                    FAIL("Failed to start transfer in 5 seconds");
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            // Wait up to 5 seconds for the transfer to complete
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            std::array<bool, nbTargets> transferComplete = {};
            do
            {
                mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
                for (size_t i = 0; i < nbTargets; i++)
                {
                    auto status = mxlFabricsTargetReadGrain(targets[i], &dummyIndex, &dummyValidSlices, std::chrono::milliseconds(20).count());
                    if (status == MXL_ERR_INTERRUPTED)
                    {
                        FAIL("Peer disconnected before the transfer completed");
                    }
                    if (status == MXL_STATUS_OK)
                    {
                        // transfer complete
                        transferComplete[i] = true;
                    }
                }
                if (std::ranges::all_of(transferComplete, [](bool v) { return v; }))
                {
                    return;
                }
            }
            while (std::chrono::steady_clock::now() < deadline);

            FAIL("Failed to complete transfer in 5 seconds");
        }

        for (size_t i = 0; i < nbTargets; i++)
        {
            mxlFabricsFreeTargetInfo(targetInfo[i]);
        }
    }

    REQUIRE(mxlFabricsRegionsFree(mxlInitiatorRegions) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    for (size_t i = 0; i < nbTargets; i++)
    {
        REQUIRE(mxlFabricsRegionsFree(mxlTargetRegions[i]) == MXL_STATUS_OK);
        REQUIRE(mxlReleaseFlowWriter(instance, writer[i]) == MXL_STATUS_OK);
        REQUIRE(mxlDestroyFlow(instance, flowIds[i].c_str()) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsDestroyTarget(fabrics, targets[i]) == MXL_STATUS_OK);
    }
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

#ifdef MXL_FABRICS_OFI
TEST_CASE("Fabrics: TargetInfo serialize/deserialize", "[fabrics][ofi][target-info]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlFabricsTarget target;
    mxlFabricsTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    auto regionsConfig = mxlFabricsExtRegionsConfig{.regions = nullptr, .regionsCount = 0, .sliceSize = {}, .format = MXL_DATA_FORMAT_VIDEO};
    mxlFabricsRegions regions;
    mxlFabricsExtGetRegions(&regionsConfig, &regions);

    auto config = mxlFabricsTargetConfig{
        .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_FABRICS_PROVIDER_TCP,
        .regions = regions,
        .deviceSupport = false
    };

    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &config, &targetInfo) == MXL_STATUS_OK);

    // Serialize the target info to a string
    size_t targetInfoStrSize = 0;
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, nullptr, &targetInfoStrSize) == MXL_STATUS_OK);
    auto targetInfoStr = std::string{};
    targetInfoStr.resize(targetInfoStrSize);
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, targetInfoStr.data(), &targetInfoStrSize) == MXL_STATUS_OK);

    // Deserialize the target info from the string
    mxlFabricsTargetInfo deserializedTargetInfo;
    REQUIRE(mxlFabricsTargetInfoFromString(targetInfoStr.c_str(), &deserializedTargetInfo) == MXL_STATUS_OK);

    // Now compare that the original and deserialized target info are the same
    auto targetInfoIn = ofi::TargetInfo::fromAPI(targetInfo);
    auto targetInfoOut = ofi::TargetInfo::fromAPI(deserializedTargetInfo);
    REQUIRE(*targetInfoIn == *targetInfoOut);

    // Cleanup
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}
#endif
