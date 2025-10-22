// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdint>
#include <cstring>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rdma/fabric.h>
#include <mxl/fabrics.h>
#include "mxl/mxl.h"
#ifdef MXL_FABRICS_OFI
// clang-format off
    #include "TargetInfo.hpp"
// clang-format on
#else
#endif

TEST_CASE("Fabrics : creation/destruction test", "[fabrics][creation/destruction]")
{
    auto instance = mxlCreateInstance("/dev/shm/", "");

    mxlFabricsInstance fabrics;
    mxlFabricsTarget target;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    auto config = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_AUTO,
        .regions = nullptr,
        .deviceSupport = false
    };

    // Clean-up
    REQUIRE(mxlFabricsTargetSetup(target, &config, &targetInfo) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE("Fabrics: Target and Ininitator connection", "[fabrics][connection][nonblocking]")
{
    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlFabricsTarget target;
    mxlFabricsInitiator initiator;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    // Setup target
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
    auto targetConfig = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_TCP,
        .regions = nullptr,
        .deviceSupport = false
    };
    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

    // Serialize the target info to a string
    size_t targetInfoStrSize = 0;
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, nullptr, &targetInfoStrSize) == MXL_STATUS_OK);
    auto targetInfoStr = std::string{};
    targetInfoStr.resize(targetInfoStrSize);
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, targetInfoStr.data(), &targetInfoStrSize) == MXL_STATUS_OK);

    // Setup initiator and add target
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
    auto initiatorConfig = mxlInitiatorConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_TCP,
        .regions = nullptr,
        .deviceSupport = false
    };
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    // try to connect them for 5 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::uint64_t dummyIndex;
    do
    {
        mxlFabricsTargetTryNewGrain(target, &dummyIndex); // make progress on target

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

TEST_CASE("Fabrics: Target and Ininitator connection blocking", "[fabrics][connection][blocking]")
{
    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlFabricsTarget target;
    mxlFabricsInitiator initiator;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    // Setup target
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
    auto targetConfig = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_TCP,
        .regions = nullptr,
        .deviceSupport = false
    };
    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

    // Serialize the target info to a string
    size_t targetInfoStrSize = 0;
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, nullptr, &targetInfoStrSize) == MXL_STATUS_OK);
    auto targetInfoStr = std::string{};
    targetInfoStr.resize(targetInfoStrSize);
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, targetInfoStr.data(), &targetInfoStrSize) == MXL_STATUS_OK);

    // Setup initiator and add target
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
    auto initiatorConfig = mxlInitiatorConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_TCP,
        .regions = nullptr,
        .deviceSupport = false
    };
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    // try to connect them for 5 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::uint64_t dummyIndex;
    do
    {
        mxlFabricsTargetWaitForNewGrain(target, &dummyIndex, std::chrono::milliseconds(20).count()); // make progress on target

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

TEST_CASE("Fabrics: Connectionless target and initiator nonblocking", "[fabrics][connectionless][nonblocking]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlFabricsTarget target;
    mxlFabricsInitiator initiator;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    // Setup target
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
    auto targetConfig = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "target", .service = "test-nonblocking"},
        .provider = MXL_SHARING_PROVIDER_SHM,
        .regions = nullptr,
        .deviceSupport = false
    };
    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

    // Serialize the target info to a string
    size_t targetInfoStrSize = 0;
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, nullptr, &targetInfoStrSize) == MXL_STATUS_OK);
    auto targetInfoStr = std::string{};
    targetInfoStr.resize(targetInfoStrSize);
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, targetInfoStr.data(), &targetInfoStrSize) == MXL_STATUS_OK);

    // Setup initiator and add target
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
    auto initiatorConfig = mxlInitiatorConfig{
        .endpointAddress = mxlEndpointAddress{.node = "initiator", .service = "test-nonblocking"},
        .provider = MXL_SHARING_PROVIDER_SHM,
        .regions = nullptr,
        .deviceSupport = false
    };
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    // try to have initiator ready for transfers within 5 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::uint64_t dummyIndex;
    do
    {
        mxlFabricsTargetTryNewGrain(target, &dummyIndex); // make progress on target

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

TEST_CASE("Fabrics: Connectionless target and initiator blocking", "[fabrics][connectionless][blocking]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlFabricsTarget target;
    mxlFabricsInitiator initiator;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    // Setup target
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
    auto targetConfig = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "target", .service = "test-blocking"},
        .provider = MXL_SHARING_PROVIDER_SHM,
        .regions = nullptr,
        .deviceSupport = false
    };
    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

    // Serialize the target info to a string
    size_t targetInfoStrSize = 0;
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, nullptr, &targetInfoStrSize) == MXL_STATUS_OK);
    auto targetInfoStr = std::string{};
    targetInfoStr.resize(targetInfoStrSize);
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, targetInfoStr.data(), &targetInfoStrSize) == MXL_STATUS_OK);

    // Setup initiator and add target
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
    auto initiatorConfig = mxlInitiatorConfig{
        .endpointAddress = mxlEndpointAddress{.node = "initiator", .service = "test-blocking"},
        .provider = MXL_SHARING_PROVIDER_SHM,
        .regions = nullptr,
        .deviceSupport = false
    };
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    // try to have initiator ready for transfers within 5 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::uint64_t dummyIndex;
    do
    {
        mxlFabricsTargetWaitForNewGrain(target, &dummyIndex, std::chrono::milliseconds(20).count()); // make progress on target

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

TEST_CASE("Fabrics: RCInitiator transfer grain", "[fabrics][connected][transfer][nonblocking]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    // Setup target regions
    mxlRegions mxlTargetRegions;
    auto targetRegion = std::vector<std::vector<std::vector<std::uint8_t>>>{
        {std::vector<std::uint8_t>(1e6)},
    };
    auto targetRegions = std::vector<mxlFabricsMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(targetRegion[0][0].data()),
         .size = targetRegion[0][0].size(),
         .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
         }
    };
    mxlFabricsRegionsFromUserBuffers(targetRegions.data(), 1, &mxlTargetRegions);

    // Setup target
    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
    auto targetConfig = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_TCP,
        .regions = mxlTargetRegions,
        .deviceSupport = false
    };
    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

    // Setup initiator regions
    mxlRegions mxlInitiatorRegions;
    auto initiatorRegion = std::vector<std::vector<std::vector<std::uint8_t>>>{
        {std::vector<std::uint8_t>(1e6)},
    };
    auto initiatorRegions = std::vector<mxlFabricsMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(initiatorRegion[0][0].data()),
         .size = initiatorRegion[0][0].size(),
         .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
         }
    };
    mxlFabricsRegionsFromUserBuffers(initiatorRegions.data(), 1, &mxlInitiatorRegions);

    // Setup intiator
    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
    auto initiatorConfig = mxlInitiatorConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_TCP,
        .regions = mxlInitiatorRegions,
        .deviceSupport = false
    };
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    // try to connect them for 5 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::uint64_t dummyIndex;
    do
    {
        mxlFabricsTargetTryNewGrain(target, &dummyIndex); // make progress on target

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

    // try to post a transfer within 5 seconds
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        mxlFabricsTargetTryNewGrain(target, &dummyIndex); // target make progress
        mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
        if (mxlFabricsInitiatorTransferGrain(initiator, 0) == MXL_STATUS_OK)
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
        auto status = mxlFabricsTargetTryNewGrain(target, &dummyIndex);
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

TEST_CASE("Fabrics: RCInitiator transfer grain", "[fabrics][connected][transfer][blocking]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    // Setup target regions
    mxlRegions mxlTargetRegions;
    auto targetRegion = std::vector<std::vector<std::vector<std::uint8_t>>>{
        {std::vector<std::uint8_t>(1e6)},
    };
    auto targetRegions = std::vector<mxlFabricsMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(targetRegion[0][0].data()),
         .size = targetRegion[0][0].size(),
         .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
         }
    };
    mxlFabricsRegionsFromUserBuffers(targetRegions.data(), 1, &mxlTargetRegions);

    // Setup target
    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
    auto targetConfig = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_TCP,
        .regions = mxlTargetRegions,
        .deviceSupport = false
    };
    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

    // Setup initiator regions
    mxlRegions mxlInitiatorRegions;
    auto initiatorRegion = std::vector<std::vector<std::vector<std::uint8_t>>>{
        {std::vector<std::uint8_t>(1e6)},
    };
    auto initiatorRegions = std::vector<mxlFabricsMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(initiatorRegion[0][0].data()),
         .size = initiatorRegion[0][0].size(),
         .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
         }
    };
    mxlFabricsRegionsFromUserBuffers(initiatorRegions.data(), 1, &mxlInitiatorRegions);

    // Setup intiator
    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
    auto initiatorConfig = mxlInitiatorConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_TCP,
        .regions = mxlInitiatorRegions,
        .deviceSupport = false
    };
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    // try to connect them for 5 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::uint64_t dummyIndex;
    do
    {
        mxlFabricsTargetTryNewGrain(target, &dummyIndex); // make progress on target

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

    // try to post a transfer within 5 seconds
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        mxlFabricsTargetWaitForNewGrain(target, &dummyIndex, std::chrono::milliseconds(20).count()); // target make progress
        mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
        if (mxlFabricsInitiatorTransferGrain(initiator, 0) == MXL_STATUS_OK)
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
        auto status = mxlFabricsTargetWaitForNewGrain(target, &dummyIndex, std::chrono::milliseconds(20).count());
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

TEST_CASE("Fabrics: RDMInitiator transfer grain", "[fabrics][ofi][connectionless][transfer][nonblocking]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    // Setup target regions
    mxlRegions mxlTargetRegions;
    auto targetRegion = std::vector<std::vector<std::vector<std::uint8_t>>>{
        {std::vector<std::uint8_t>(1e6)},
    };
    auto targetRegions = std::vector<mxlFabricsMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(targetRegion[0][0].data()),
         .size = targetRegion[0][0].size(),
         .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
         }
    };
    mxlFabricsRegionsFromUserBuffers(targetRegions.data(), 1, &mxlTargetRegions);

    // Setup target
    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
    auto targetConfig = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "target", .service = "test-nonblocking"},
        .provider = MXL_SHARING_PROVIDER_SHM,
        .regions = mxlTargetRegions,
        .deviceSupport = false
    };
    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

    // Setup initiator regions
    mxlRegions mxlInitiatorRegions;
    auto initiatorRegion = std::vector<std::vector<std::vector<std::uint8_t>>>{
        {std::vector<std::uint8_t>(1e6)},
    };
    auto initiatorRegions = std::vector<mxlFabricsMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(initiatorRegion[0][0].data()),
         .size = initiatorRegion[0][0].size(),
         .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
         }
    };
    mxlFabricsRegionsFromUserBuffers(targetRegions.data(), 1, &mxlInitiatorRegions);

    // Setup intiator
    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
    auto initiatorConfig = mxlInitiatorConfig{
        .endpointAddress = mxlEndpointAddress{.node = "initiator", .service = "test-nonblocking"},
        .provider = MXL_SHARING_PROVIDER_SHM,
        .regions = mxlInitiatorRegions,
        .deviceSupport = false
    };
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    // try to connect them for 5 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::uint64_t dummyIndex;
    do
    {
        mxlFabricsTargetTryNewGrain(target, &dummyIndex); // make progress on target

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

    // try to post a transfer within 5 seconds
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        mxlFabricsTargetTryNewGrain(target, &dummyIndex); // target make progress
        mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
        if (mxlFabricsInitiatorTransferGrain(initiator, 0) == MXL_STATUS_OK)
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

    // wait up to 5 seconds for the transfer to complete
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
        auto status = mxlFabricsTargetTryNewGrain(target, &dummyIndex);
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

TEST_CASE("Fabrics: RDMInitiator transfer grain", "[fabrics][ofi][connectionless][transfer][blocking]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    // Setup target regions
    mxlRegions mxlTargetRegions;
    auto targetRegion = std::vector<std::vector<std::vector<std::uint8_t>>>{
        {std::vector<std::uint8_t>(1e6)},
    };
    auto targetRegions = std::vector<mxlFabricsMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(targetRegion[0][0].data()),
         .size = targetRegion[0][0].size(),
         .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
         }
    };
    mxlFabricsRegionsFromUserBuffers(targetRegions.data(), 1, &mxlTargetRegions);

    // Setup target
    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
    auto targetConfig = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "target", .service = "test-blocking"},
        .provider = MXL_SHARING_PROVIDER_SHM,
        .regions = mxlTargetRegions,
        .deviceSupport = false
    };
    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, &targetInfo) == MXL_STATUS_OK);

    // Setup initiator regions
    mxlRegions mxlInitiatorRegions;
    auto initiatorRegion = std::vector<std::vector<std::vector<std::uint8_t>>>{
        {std::vector<std::uint8_t>(1e6)},
    };
    auto initiatorRegions = std::vector<mxlFabricsMemoryRegion>{
        {
         .addr = reinterpret_cast<std::uintptr_t>(initiatorRegion[0][0].data()),
         .size = initiatorRegion[0][0].size(),
         .loc = {.type = MXL_MEMORY_REGION_TYPE_HOST, .deviceId = 0},
         }
    };
    mxlFabricsRegionsFromUserBuffers(targetRegions.data(), 1, &mxlInitiatorRegions);

    // Setup intiator
    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
    auto initiatorConfig = mxlInitiatorConfig{
        .endpointAddress = mxlEndpointAddress{.node = "initiator", .service = "test-blocking"},
        .provider = MXL_SHARING_PROVIDER_SHM,
        .regions = mxlInitiatorRegions,
        .deviceSupport = false
    };
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    // try to connect them for 5 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::uint64_t dummyIndex;
    do
    {
        mxlFabricsTargetTryNewGrain(target, &dummyIndex); // make progress on target
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

    // try to post a transfer within 5 seconds
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        mxlFabricsTargetWaitForNewGrain(target, &dummyIndex, std::chrono::milliseconds(20).count()); // target make progress
        mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
        if (mxlFabricsInitiatorTransferGrain(initiator, 0) == MXL_STATUS_OK)
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

    // wait up to 5 seconds for the transfer to complete
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        mxlFabricsInitiatorMakeProgressBlocking(initiator, std::chrono::milliseconds(20).count());
        auto status = mxlFabricsTargetWaitForNewGrain(target, &dummyIndex, std::chrono::milliseconds(20).count());
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

#ifdef MXL_FABRICS_OFI
TEST_CASE("Fabrics: TargetInfo serialize/deserialize", "[fabrics][ofi][target-info]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance("/dev/shm/", "");
    mxlFabricsInstance fabrics;
    mxlFabricsTarget target;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    auto config = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_SHARING_PROVIDER_TCP,
        .regions = nullptr,
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
    mxlTargetInfo deserializedTargetInfo;
    REQUIRE(mxlFabricsTargetInfoFromString(targetInfoStr.c_str(), &deserializedTargetInfo) == MXL_STATUS_OK);

    // Now compare that the original and deserialized target info are the same
    auto targetInfoIn = ofi::TargetInfo::fromAPI(targetInfo);
    auto targetInfoOut = ofi::TargetInfo::fromAPI(deserializedTargetInfo);
    REQUIRE(*targetInfoIn == *targetInfoOut);

    // Cleanup
    REQUIRE(mxlFabricsTargetSetup(target, &config, &targetInfo) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}
#endif
