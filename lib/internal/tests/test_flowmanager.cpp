// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <limits>
#include <list>
#include <memory>
#include <thread>
#include <unistd.h>
#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include "mxl-internal/DiscreteFlowData.hpp"
#include "mxl-internal/FlowManager.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"
#include "../../tests/Utils.hpp"

using namespace mxl::lib;

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow Manager : Create Manager", "[flow manager]")
{
    remove_all(domain);
    // This should throw since the folder should not exist.
    REQUIRE_THROWS(
        [&]()
        {
            std::make_shared<FlowManager>(domain);
        }());

    // Create the mxl domain path.
    REQUIRE(create_directory(domain));

    auto const manager = std::make_shared<FlowManager>(domain);
    REQUIRE(manager->listFlows().size() == 0);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow Manager : Create Video Flow Structure", "[flow manager]")
{
    auto const flowDef = mxl::tests::readFile("data/v210_flow.json");
    auto const flowId = *uuids::uuid::from_string("5fbec3b1-1b0f-417d-9059-8b94a47197ed");
    auto const grainRate = mxlRational{60000, 1001};

    auto const payloadSize = 1024;
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};

    auto manager = std::make_shared<FlowManager>(domain);
    auto [created, flowData] = manager->createOrOpenDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, 5, grainRate, payloadSize, 1, sliceSizes);

    REQUIRE(flowData != nullptr);
    REQUIRE(flowData->isValid());
    REQUIRE(flowData->grainCount() == 5);

    auto const flowDirectory = makeFlowDirectoryName(domain, uuids::to_string(flowId));
    REQUIRE(exists(flowDirectory));
    REQUIRE(is_directory(flowDirectory));

    // Check that the flow SHM storage exists
    auto const flowFile = makeFlowDataFilePath(flowDirectory);
    REQUIRE(exists(flowFile));
    REQUIRE(is_regular_file(flowFile));

    // Check that the flow access file for the SHM storage exists
    auto const flowAccessFile = makeFlowAccessFilePath(flowDirectory);
    REQUIRE(exists(flowAccessFile));
    REQUIRE(is_regular_file(flowAccessFile));

    // Check that the resource definition exists and is a regular file
    auto const resourceDefinitionFile = makeFlowDescriptorFilePath(flowDirectory);
    REQUIRE(exists(resourceDefinitionFile));
    REQUIRE(is_regular_file(resourceDefinitionFile));

    // Check that the resource definition contains a literal copy of the
    // definiton passed to the manager.
    REQUIRE(mxl::tests::readFile(resourceDefinitionFile) == flowDef);

    // Expect no channel data storage in this flow
    auto const channelDataFile = makeChannelDataFilePath(flowDirectory);
    REQUIRE(!exists(channelDataFile));

    // Count the grains.
    auto const grainDir = makeGrainDirectoryName(flowDirectory);
    REQUIRE(exists(grainDir));
    REQUIRE(is_directory(grainDir));

    auto grainCount = std::size_t{0};
    for (auto const& entry : std::filesystem::directory_iterator{grainDir})
    {
        if (is_regular_file(entry))
        {
            grainCount += 1U;
        }
    }
    REQUIRE(grainCount == 5U);

    // Should not throw, existing flow should be opened.
    {
        auto [created, flow] = manager->createOrOpenDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, 5, grainRate, payloadSize, 1, sliceSizes);
        REQUIRE_FALSE(created);
    }

    bool thrown = false;
    // Should throw, because the existing flow is a discrete flow.
    try
    {
        auto const sampleRate = mxlRational{48000, 1};
        auto [flow, created] = manager->createOrOpenContinuousFlow(flowId, flowDef, MXL_DATA_FORMAT_AUDIO, sampleRate, 8, sizeof(float), 8192);
    }
    catch (std::exception const& ex)
    {
        MXL_ERROR(ex.what());
        thrown = true;
    }

    // For some reason REQUIRE_THROWS([](){}) does not work as expected.
    REQUIRE(thrown);

    REQUIRE(manager->listFlows().size() == 1);

    // Close the flow.  it should not be available for a get operation after being closed.
    flowData.reset();

    REQUIRE(manager->listFlows().size() == 1);

    // Delete the flow.
    manager->deleteFlow(flowId);

    REQUIRE(manager->listFlows().size() == 0);

    // Confirm that files on disk do not exist anymore
    REQUIRE(!exists(flowDirectory));
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow Manager : Per-grain files remain the default", "[flow manager][grain pool]")
{
    auto const flowDef = mxl::tests::readFile("data/v210_flow.json");
    auto const flowId = *uuids::uuid::from_string("16fd2706-8baf-433b-82eb-8c7fada847da");
    auto const grainRate = mxlRational{60000, 1001};

    auto const grainCount = std::size_t{3};
    auto const payloadSize = std::size_t{512};
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{static_cast<std::uint32_t>(payloadSize), 0, 0, 0};

    auto manager = std::make_shared<FlowManager>(domain);

    // Default call (no layout option) must keep the per-grain file layout.
    auto [created,
        flowData] = manager->createOrOpenDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, grainCount, grainRate, payloadSize, 1, sliceSizes);

    REQUIRE(created);
    REQUIRE(flowData != nullptr);
    REQUIRE_FALSE(flowData->isWindowMode());
    REQUIRE(flowData->grainCount() == grainCount);

    auto const flowDirectory = makeFlowDirectoryName(domain, uuids::to_string(flowId));
    auto const grainDir = makeGrainDirectoryName(flowDirectory);
    REQUIRE(exists(makeGrainDataFilePath(grainDir, 0)));
    REQUIRE((flowData->flowInfo()->config.common.flags & MXL_FLOW_FLAG_CONTIGUOUS_GRAINS) == 0U);

    flowData.reset();
    manager->deleteFlow(flowId);
    REQUIRE(!exists(flowDirectory));
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow Manager : Contiguous per-grain files", "[flow manager][grain pool]")
{
    auto const flowDef = mxl::tests::readFile("data/v210_flow.json");
    auto const flowId = *uuids::uuid::from_string("2b1e0f9c-3d4a-4b5c-8e6f-1a2b3c4d5e6f");
    auto const grainRate = mxlRational{60000, 1001};

    auto const grainCount = std::size_t{5};
    auto const payloadSize = std::size_t{1024};
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{static_cast<std::uint32_t>(payloadSize), 0, 0, 0};

    auto manager = std::make_shared<FlowManager>(domain);

    // Create the flow with the contiguous per-grain-file layout enabled.
    auto [created, flowData] = manager->createOrOpenDiscreteFlow(flowId,
        flowDef,
        MXL_DATA_FORMAT_VIDEO,
        grainCount,
        grainRate,
        payloadSize,
        1,
        sliceSizes,
        1,
        1,
        /*useContiguousGrains=*/true);

    REQUIRE(created);
    REQUIRE(flowData != nullptr);
    REQUIRE(flowData->isValid());
    REQUIRE(flowData->isWindowMode());
    REQUIRE(flowData->grainCount() == grainCount);

    // The grains must be laid out contiguously in virtual memory at a fixed,
    // page-aligned stride even though they are backed by individual files.
    auto const stride = DiscreteFlowData::grainStride(payloadSize);
    REQUIRE(stride >= sizeof(Grain) + payloadSize);
    REQUIRE((stride % static_cast<std::size_t>(sysconf(_SC_PAGESIZE))) == 0U);

    auto const* grain0 = reinterpret_cast<std::uint8_t const*>(flowData->grainAt(0));
    REQUIRE(grain0 != nullptr);
    for (auto i = std::size_t{0}; i < grainCount; ++i)
    {
        auto const* grain = flowData->grainAt(i);
        REQUIRE(grain != nullptr);
        REQUIRE(flowData->grainMappedSize(i) == sizeof(Grain) + payloadSize);
        REQUIRE(reinterpret_cast<std::uint8_t const*>(grain) == grain0 + i * stride);
        REQUIRE(grain->header.info.version == GRAIN_HEADER_VERSION);
        REQUIRE(grain->header.info.grainSize == payloadSize);
    }

    // On disk, the per-grain files must exist.
    auto const flowDirectory = makeFlowDirectoryName(domain, uuids::to_string(flowId));
    auto const grainDir = makeGrainDirectoryName(flowDirectory);
    for (auto i = std::size_t{0}; i < grainCount; ++i)
    {
        REQUIRE(exists(makeGrainDataFilePath(grainDir, i)));
    }

    // The layout flag must be recorded so readers can reconstruct the mapping.
    REQUIRE((flowData->flowInfo()->config.common.flags & MXL_FLOW_FLAG_CONTIGUOUS_GRAINS) != 0U);

    // A reader opening the same flow must reconstruct the contiguous mapping.
    {
        auto readerBase = manager->openFlow(flowId, AccessMode::READ_ONLY);
        auto* readerFlow = dynamic_cast<DiscreteFlowData*>(readerBase.get());
        REQUIRE(readerFlow != nullptr);
        REQUIRE(readerFlow->isWindowMode());
        REQUIRE(readerFlow->grainCount() == grainCount);

        auto const* readerGrain0 = reinterpret_cast<std::uint8_t const*>(readerFlow->grainAt(0));
        REQUIRE(readerGrain0 != nullptr);
        for (auto i = std::size_t{0}; i < grainCount; ++i)
        {
            auto const* grain = readerFlow->grainAt(i);
            REQUIRE(grain != nullptr);
            REQUIRE(reinterpret_cast<std::uint8_t const*>(grain) == readerGrain0 + i * stride);
            REQUIRE(grain->header.info.grainSize == payloadSize);
        }
    }

    flowData.reset();
    manager->deleteFlow(flowId);
    REQUIRE(!exists(flowDirectory));
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Contiguous grain window can be reset before fallback", "[flow manager]")
{
    auto const flowPath = domain / "flow.data";
    auto const grainPath = domain / "grain.data";
    constexpr auto payloadSize = std::size_t{1024};

    auto flowData = DiscreteFlowData{flowPath.string().c_str(), AccessMode::CREATE_READ_WRITE, LockMode::Shared};
    auto const stride = DiscreteFlowData::grainStride(payloadSize);
    REQUIRE(flowData.openContiguousWindow(2U, stride));
    REQUIRE(flowData.emplaceGrainAt(0U, grainPath.string().c_str(), payloadSize) != nullptr);
    REQUIRE(flowData.isWindowMode());
    REQUIRE(flowData.grainCount() == 1U);

    flowData.resetContiguousWindow();
    REQUIRE_FALSE(flowData.isWindowMode());
    REQUIRE(flowData.grainCount() == 0U);

    REQUIRE(flowData.emplaceGrain(grainPath.string().c_str(), payloadSize) != nullptr);
    REQUIRE_FALSE(flowData.isWindowMode());
    REQUIRE(flowData.grainCount() == 1U);
    REQUIRE(flowData.grainMappedSize(0U) == sizeof(Grain) + payloadSize);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow Manager : Rejects inconsistent contiguous grain files", "[flow manager]")
{
    auto const flowDef = mxl::tests::readFile("data/v210_flow.json");
    auto const flowId = *uuids::uuid::from_string("4a5b6c7d-8e9f-4012-a345-6789abcdef01");
    auto const grainRate = mxlRational{60000, 1001};
    auto const grainCount = std::size_t{3};
    auto const payloadSize = std::size_t{1024};
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{static_cast<std::uint32_t>(payloadSize), 0, 0, 0};
    auto manager = std::make_shared<FlowManager>(domain);

    auto [created, flowData] = manager->createOrOpenDiscreteFlow(flowId,
        flowDef,
        MXL_DATA_FORMAT_VIDEO,
        grainCount,
        grainRate,
        payloadSize,
        1,
        sliceSizes,
        1,
        1,
        /*useContiguousGrains=*/true);
    REQUIRE(created);

    auto const grainDir = makeGrainDirectoryName(makeFlowDirectoryName(domain, uuids::to_string(flowId)));
    flowData.reset();

    SECTION("a later grain exceeds its slot")
    {
        resize_file(makeGrainDataFilePath(grainDir, 1U), DiscreteFlowData::grainStride(payloadSize) + 1U);
        REQUIRE_THROWS_AS(manager->openFlow(flowId, AccessMode::READ_ONLY), std::invalid_argument);
    }

    SECTION("the first grain is smaller than its header")
    {
        resize_file(makeGrainDataFilePath(grainDir, 0U), sizeof(Grain) - 1U);
        REQUIRE_THROWS(manager->openFlow(flowId, AccessMode::READ_ONLY));
    }

    manager->deleteFlow(flowId);
}

TEST_CASE("Contiguous grain sizing rejects overflow", "[flow manager]")
{
    auto const maximum = std::numeric_limits<std::size_t>::max();
    REQUIRE(DiscreteFlowData::grainStride(maximum) == 0U);
    REQUIRE(ContiguousWindow::alignToPage(maximum) == 0U);

    auto window = ContiguousWindow{maximum, 2U};
    REQUIRE_FALSE(window.valid());
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow Manager : Create Audio Flow Structure", "[flow manager]")
{
    auto const flowDef = mxl::tests::readFile("data/audio_flow.json");
    auto const flowId = *uuids::uuid::from_string("b3bb5be7-9fe9-4324-a5bb-4c70e1084449");
    auto const flowString = to_string(flowId);
    auto const sampleRate = mxlRational{48000, 1};

    auto manager = std::make_shared<FlowManager>(domain);
    auto [created, flowData] = manager->createOrOpenContinuousFlow(flowId, flowDef, MXL_DATA_FORMAT_AUDIO, sampleRate, 2, sizeof(float), 4096);

    REQUIRE(flowData != nullptr);
    REQUIRE(flowData->isValid());
    REQUIRE(flowData->channelCount() == 2U);
    REQUIRE(flowData->sampleWordSize() == sizeof(float));
    REQUIRE(flowData->channelBufferLength() == 4096U);
    REQUIRE(flowData->channelDataLength() == (flowData->channelCount() * flowData->channelBufferLength()));
    REQUIRE(flowData->channelDataSize() == (flowData->channelDataLength() * flowData->sampleWordSize()));

    auto const flowDirectory = makeFlowDirectoryName(domain, uuids::to_string(flowId));
    REQUIRE(exists(flowDirectory));
    REQUIRE(is_directory(flowDirectory));

    // Check that the flow SHM storage exists
    auto const flowFile = makeFlowDataFilePath(flowDirectory);
    REQUIRE(exists(flowFile));
    REQUIRE(is_regular_file(flowFile));

    // Check that the resource definition exists and is a regular file
    auto const resourceDefinitionFile = makeFlowDescriptorFilePath(flowDirectory);
    REQUIRE(exists(resourceDefinitionFile));
    REQUIRE(is_regular_file(resourceDefinitionFile));

    // Check that the resource definition contains a literal copy of the
    // definiton passed to the manager.
    REQUIRE(mxl::tests::readFile(resourceDefinitionFile) == flowDef);

    // Check that the channel data SHM storage exists
    auto const channelDataFile = makeChannelDataFilePath(flowDirectory);
    REQUIRE(exists(channelDataFile));
    REQUIRE(is_regular_file(channelDataFile));

    // Expect no grains in this flow
    auto const grainDir = makeGrainDirectoryName(flowDirectory);
    REQUIRE(!exists(grainDir));

    {
        auto [created, flowData] = manager->createOrOpenContinuousFlow(flowId, flowDef, MXL_DATA_FORMAT_AUDIO, sampleRate, 8, sizeof(float), 8192);
        REQUIRE_FALSE(created);
    }

    auto const payloadSize = 1024;
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};

    // This should throw since the flow exists but is in a different format.
    REQUIRE_THROWS(
        [&]()
        {
            auto const grainRate = mxlRational{60000, 1001};
            manager->createOrOpenDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, 5, grainRate, payloadSize, 1, sliceSizes);
        }());

    REQUIRE(manager->listFlows().size() == 1);

    // Close the flow. It should not be available for a get operation after being closed.
    flowData.reset();

    REQUIRE(manager->listFlows().size() == 1);

    // Delete the flow.
    manager->deleteFlow(flowId);

    REQUIRE(manager->listFlows().size() == 0);

    // Confirm that files on disk do not exist anymore
    REQUIRE(!exists(flowDirectory));
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow Manager : Open, List, and Error Conditions", "[flow manager]")
{
    auto manager = std::make_shared<FlowManager>(domain);

    //
    // 1) Create & open a discrete flow
    //
    auto const flowId1 = *uuids::uuid::from_string("11111111-1111-1111-1111-111111111111");
    auto const flowDef1 = mxl::tests::readFile("data/v210_flow.json");
    auto const grainRate = mxlRational{60000, 1001};
    {
        auto const payloadSize = 512;
        auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};

        auto [created,
            flowData1] = manager->createOrOpenDiscreteFlow(flowId1, flowDef1, MXL_DATA_FORMAT_VIDEO, 3, grainRate, payloadSize, 0, sliceSizes);
        REQUIRE(flowData1->grainCount() == 3U);
        // close writer
        flowData1.reset();
    }
    // open in read-only mode
    {
        auto openData1 = manager->openFlow(flowId1, AccessMode::READ_ONLY);
        REQUIRE(openData1);
        auto* d = dynamic_cast<DiscreteFlowData*>(openData1.get());
        REQUIRE(d);
        REQUIRE(d->grainCount() == 3U);
    }

    //
    // 2) Create & open a continuous flow
    //
    auto const flowId2 = *uuids::uuid::from_string("22222222-2222-2222-2222-222222222222");
    auto const flowDef2 = mxl::tests::readFile("data/audio_flow.json");
    auto const sampleRate = mxlRational{48000, 1};
    {
        auto [created, flowData2] = manager->createOrOpenContinuousFlow(flowId2, flowDef2, MXL_DATA_FORMAT_AUDIO, sampleRate, 4, sizeof(float), 2048);
        REQUIRE(flowData2->channelCount() == 4U);
        flowData2.reset();
    }
    {
        auto openData2 = manager->openFlow(flowId2, AccessMode::READ_WRITE);
        REQUIRE(openData2);
        auto* c = dynamic_cast<ContinuousFlowData*>(openData2.get());
        REQUIRE(c);
        REQUIRE(c->channelCount() == 4U);
    }

    //
    // 3) listFlows should report both flows
    //
    {
        auto flows = manager->listFlows();
        REQUIRE(flows.size() == 2);
    }

    //
    // 4) deleteFlow(nullptr) returns false
    //
    {
        auto empty = std::unique_ptr<FlowData>{};
        REQUIRE(manager->deleteFlow(std::move(empty)) == false);
    }

    //
    // 5) delete by ID and verify removal
    //
    REQUIRE(manager->deleteFlow(flowId1));
    REQUIRE(manager->listFlows().size() == 1);
    REQUIRE(manager->deleteFlow(flowId2));
    REQUIRE(manager->listFlows().empty());

    //
    // 6) openFlow invalid mode should throw
    //
    REQUIRE_THROWS_AS(manager->openFlow(flowId1, AccessMode::CREATE_READ_WRITE), std::invalid_argument);

    //
    // 7) opening a non-existent flow throws filesystem_error
    //
    auto const flowId3 = *uuids::uuid::from_string("33333333-3333-3333-3333-333333333333");
    REQUIRE_THROWS_AS(manager->openFlow(flowId3, AccessMode::READ_ONLY), std::filesystem::filesystem_error);

    //
    // 8) listFlows skips invalid directories
    //
    {
        // manually drop a bogus folder
        auto invalidDir = domain / "not-a-valid-uuid.mxl-flow";
        REQUIRE(create_directory(invalidDir));
        manager = std::make_shared<FlowManager>(domain);
        auto flows2 = manager->listFlows();
        REQUIRE(flows2.empty());
    }

    //
    // 9) listFlows on missing domain throws
    //
    remove_all(domain);
    REQUIRE_THROWS_AS(manager->listFlows(), std::filesystem::filesystem_error);

    //
    // 10) unsupported formats should be rejected
    //
    auto const payloadSize = 512;
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};
    auto const badId = *uuids::uuid::from_string("44444444-4444-4444-4444-444444444444");
    REQUIRE_THROWS_AS(manager->createOrOpenDiscreteFlow(badId, flowDef1, MXL_DATA_FORMAT_UNSPECIFIED, 1, grainRate, payloadSize, 1, sliceSizes),
        std::runtime_error);
    REQUIRE_THROWS_AS(manager->createOrOpenContinuousFlow(badId, flowDef2, MXL_DATA_FORMAT_VIDEO, sampleRate, 1, 4, 1024), std::runtime_error);
}

// Re-creation after deletion
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "FlowManager: re-create flow after deletion", "[flow manager][reuse]")
{
    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = mxlRational{50, 1};

    auto const payloadSize = 512;
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};
    // Create discrete flow #1
    auto [created1, f1] = mgr->createOrOpenDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, payloadSize, 1, sliceSizes);
    REQUIRE(f1);
    REQUIRE(mgr->listFlows().size() == 1);

    // Delete it
    REQUIRE(mgr->deleteFlow(id));
    REQUIRE(mgr->listFlows().empty());

    // Re-create with same ID
    auto [created2, f2] = mgr->createOrOpenDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 2, rate, payloadSize, 1, sliceSizes);
    REQUIRE(f2);
    REQUIRE(mgr->listFlows().size() == 1);
}

// Test robustness: corrupted flow directory with missing descriptor
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "FlowManager: corrupted flow with missing descriptor", "[flow manager][corruption]")
{
    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = mxlRational{50, 1};

    // Create and publish
    auto const payloadSize = 512;
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};
    auto [created, flow] = mgr->createOrOpenDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, payloadSize, 1, sliceSizes);
    flow.reset();
    REQUIRE(mgr->listFlows().size() == 1);

    // Manually remove the descriptor JSON to simulate corruption
    auto flowDir = makeFlowDirectoryName(domain, uuids::to_string(id));
    auto descFile = makeFlowDescriptorFilePath(flowDir);
    REQUIRE(exists(descFile));
    remove(descFile);

    // By design decision, corrupted flows are not filtered out
    auto ids = mgr->listFlows();
    REQUIRE(ids.size() == 1);

    auto rd = mgr->openFlow(id, AccessMode::READ_ONLY);
    REQUIRE(rd);
    auto* d = dynamic_cast<DiscreteFlowData*>(rd.get());
    REQUIRE(d);
    REQUIRE(d->grainCount() == 1U);
}

// Concurrent createDiscreteFlow with same UUID
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "FlowManager: concurrent createDiscreteFlow same UUID", "[flow manager][concurrency]")
{
    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("cccccccc-cccc-cccc-cccc-cccccccccccc");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = mxlRational{50, 1};

    auto success = std::atomic<int>{0};
    auto failure = std::atomic<int>{0};
    auto worker = [&](std::uint32_t payloadSize)
    {
        auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};
        auto [created, f] = mgr->createOrOpenDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, payloadSize, 1, sliceSizes);
        if (created)
        {
            ++success;
        }
        else
        {
            ++failure;
        }
    };

    auto t1 = std::thread{worker, 64u};
    auto t2 = std::thread{worker, 128u};
    t1.join();
    t2.join();

    // Exactly one must succeed, and one must fail
    REQUIRE(success.load() == 1);
    REQUIRE(failure.load() == 1);
    REQUIRE(mgr->listFlows().size() == 1);
}

// deleteFlow on read-only domain
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "FlowManager: deleteFlow on read-only domain", "[flow manager][deletion]")
{
    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("dddddddd-dddd-dddd-dddd-dddddddddddd");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = mxlRational{50, 1};

    auto const payloadSize = 512;
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};
    auto [created, f] = mgr->createOrOpenDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, payloadSize, 1, sliceSizes);
    f.reset();
    REQUIRE(mgr->listFlows().size() == 1);

    // Make domain read-only
    permissions(domain,
        std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all,
        std::filesystem::perm_options::remove);

    // DeleteFlow should respect filesystem permissions but return false instead of throwing
    // When domain is read-only, deletion operations fail and method returns false
    // This test verifies that permission errors are handled gracefully
    REQUIRE(mgr->deleteFlow(id) == false);

    // Restore full permissions for cleanup and verification
    permissions(domain, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);

    // Since deletion failed due to permissions, the flow should still exist
    REQUIRE(mgr->listFlows().size() == 1);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "FlowManager: deleteFlow returns false for non-existent flow", "[flow manager][deletion]")
{
    auto mgr = std::make_shared<FlowManager>(domain);
    auto const nonExistentId = *uuids::uuid::from_string("99999999-9999-9999-9999-999999999999");

    // Try to delete a flow that doesn't exist
    REQUIRE(mgr->deleteFlow(nonExistentId) == false);
}

// Concurrent listFlows + deleteFlow does not crash
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "FlowManager: concurrent listFlows and deleteFlow", "[flow manager][concurrency]")
{
    auto mgr = std::make_shared<FlowManager>(domain);
    auto ids = std::vector<uuids::uuid>{};
    for (int i = 0; i < 5; ++i)
    {
        // Last segment must be exactly 12 hex digits: 11 zeros + one hex digit
        auto id = *uuids::uuid::from_string(fmt::format("eeeeeeee-eeee-eeee-eeee-00000000000{:X}", i));
        ids.push_back(id);

        auto const payloadSize = 512;
        auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};
        mgr->createOrOpenDiscreteFlow(
            id, mxl::tests::readFile("data/v210_flow.json"), MXL_DATA_FORMAT_VIDEO, 1, mxlRational{50, 1}, payloadSize, 1, sliceSizes);
    }

    auto done = std::atomic<bool>{false};
    auto lister = std::thread{[&]()
        {
            while (!done)
            {
                auto list = mgr->listFlows();
                // no crash, and size never exceeds 5
                REQUIRE(list.size() <= 5);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }};

    // Delete them one by one
    for (auto& id : ids)
    {
        mgr->deleteFlow(id);
    }
    done = true;
    lister.join();

    REQUIRE(mgr->listFlows().empty());
}

// Multiple FlowManager instances on same domain
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "FlowManager: multiple instances share domain", "[flow manager]")
{
    auto domain = mxl::tests::makeTempDomain();
    auto mgr1 = std::make_shared<FlowManager>(domain);
    auto mgr2 = std::make_shared<FlowManager>(domain);

    auto const id = *uuids::uuid::from_string("ffffffff-ffff-ffff-ffff-ffffffffffff");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = mxlRational{50, 1};

    auto const payloadSize = 512;
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};

    auto f1 = mgr1->createOrOpenDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, payloadSize, 1, sliceSizes);
    REQUIRE(mgr1->listFlows().size() == 1);
    // mgr2 should see it too
    REQUIRE(mgr2->listFlows().size() == 1);

    // Delete via mgr2
    REQUIRE(mgr2->deleteFlow(id));
    REQUIRE(mgr1->listFlows().empty());
    REQUIRE(mgr2->listFlows().empty());
}

// createFlow on unwritable domain
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "FlowManager: createFlow throws on unwritable domain", "[flow manager][error]")
{
    // remove write perms on domain
    permissions(domain, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);

    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("aaaaaaaa-0000-0000-0000-000000000000");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = mxlRational{50, 1};

    // CreateFlow should respect filesystem permissions
    // When domain is unwritable, creation operations should fail
    // This test verifies proper permission enforcement
    auto const payloadSize = 512;
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};

    REQUIRE_THROWS_AS(mgr->createOrOpenDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, payloadSize, 1, sliceSizes),
        std::filesystem::filesystem_error);

    // restore perms so we can clean up
    permissions(domain, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "FlowManager: parallel flow writer creation", "[flow manager]")
{
    auto const numIterations = 10'000;
    auto const numWorkers = 3;

    struct Worker
    {
        std::unique_ptr<std::atomic<bool>> readySignal;
        std::unique_ptr<FlowManager> mgr;
        std::optional<std::thread> thread;
    };

    auto flowManager = std::make_unique<FlowManager>(domain);
    auto workers = std::list<Worker>{};

    auto createdSignal = std::atomic<int>{0};        // Signal from each worker that they are done creating/opening the flow.
    auto numOpened = std::atomic<std::uint64_t>{0};  // Total number of flows that were opened.
    auto numCreated = std::atomic<std::uint64_t>{0}; // Total number of flows that were created.

    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const id = *uuids::uuid::from_string("5fbec3b1-1b0f-417d-9059-8b94a47197ed");
    auto const payloadSize = 512;
    auto const sliceSizes = std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN>{payloadSize, 0, 0, 0};

    auto workFn = [&](Worker* worker, int workerIndex)
    {
        for (auto i = 0; i < numIterations; ++i)
        {
            // Wait for the main threads signal to start
            while (!worker->readySignal->exchange(false))
            {
            }

            {
                // Create or open the flow. This is where the race happens. Only one of the workers should be able to create the flow (created ==
                // true). The others should still receive an opened flow that is writable, but created should be false.
                auto [created,
                    flow] = worker->mgr->createOrOpenDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, mxlRational{50, 1}, payloadSize, 1, sliceSizes);

                // Writes the current iteration number into the padding of the header of the first grain.
                *(reinterpret_cast<std::uint64_t*>(&flow->grainAt(0)->header.pad) + workerIndex) = i;

                // Increment either numCreated or numOpened.
                (created ? &numCreated : &numOpened)->fetch_add(1);
            }

            // Signal that we have completed to iteration
            createdSignal.fetch_add(1);
        }
    };

    // Create and start all the workers.
    for (auto i = 0; i < numWorkers; ++i)
    {
        auto& w = workers.emplace_back(std::make_unique<std::atomic<bool>>(false), std::make_unique<FlowManager>(domain), std::nullopt);
        w.thread = std::thread{workFn, &w, i};
    }

    for (auto i = 0; i < numIterations; ++i)
    {
        // Signal all workers
        std::ranges::for_each(workers, [](Worker& w) { w.readySignal->store(true); });

        // Wait for the workers to have opened and written to the shared memory
        while (createdSignal.load() != static_cast<int>(workers.size()))
        {
        }
        createdSignal.store(0);

        // The flow should exist
        REQUIRE(flowDirectoryExists(uuids::to_string(id)));

        {
            // We should be able to open the flow.
            auto [created,
                flow] = flowManager->createOrOpenDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, mxlRational{50, 1}, payloadSize, 1, sliceSizes);
            REQUIRE_FALSE(created); // The existing flow should be opened and not a new one created

            // Prove that all workers have actually opened the same memory by checking the grain header padding.
            // The workers should all have written the current iteration index here.
            auto paddingStorage = reinterpret_cast<std::uint64_t*>(&flow->grainAt(0)->header.pad);
            for (std::size_t w = 0; w < workers.size(); ++w)
            {
                REQUIRE(paddingStorage[w] == static_cast<std::uint64_t>(i));
            }
        }

        // Delete the flow.
        flowManager->deleteFlow(id);
    }

    std::ranges::for_each(workers, [](Worker& w) { w.thread->join(); });

    // In each iteration the flow should have been created only once
    REQUIRE(numCreated.load() == numIterations);

    // In each iteration all workers that did not create the flow should have opened it.
    REQUIRE(numOpened.load() == numIterations * (numWorkers - 1));
}
