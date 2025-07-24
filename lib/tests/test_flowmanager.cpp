#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include "../src/internal/FlowManager.hpp"
#include "../src/internal/PathUtils.hpp"
#include "Utils.hpp"

using namespace mxl::lib;

namespace
{
    // Helper to make a unique temp domain
    std::filesystem::path makeTempDomain()
    {
        char tmpl[] = "/dev/shm/mxl_test_domainXXXXXX";
        REQUIRE(mkdtemp(tmpl) != nullptr); // mkdtemp is in global namespace
        return std::filesystem::path{tmpl};
    }
}

TEST_CASE("Flow Manager : Create Manager", "[flow manager]")
{
    auto const domain = mxl::tests::getDomainPath();
    // Remove that path if it exists.
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

TEST_CASE("Flow Manager : Create Video Flow Structure", "[flow manager]")
{
    auto const domain = mxl::tests::getDomainPath();
    // Clean out the mxl domain path, if it exists.
    remove_all(domain);
    REQUIRE(create_directory(domain));

    auto const flowDef = mxl::tests::readFile("data/v210_flow.json");
    auto const flowId = *uuids::uuid::from_string("5fbec3b1-1b0f-417d-9059-8b94a47197ed");
    auto const grainRate = Rational{60000, 1001};

    auto manager = std::make_shared<FlowManager>(domain);
    auto flowData = manager->createDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, 5, grainRate, 1024);

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

    // This should throw since the flow metadata will already exist.
    REQUIRE_THROWS(
        [&]()
        {
            manager->createDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, 5, grainRate, 1024);
        }());

    // This should throw since the flow metadata will already exist.
    REQUIRE_THROWS(
        [&]()
        {
            auto const sampleRate = Rational{48000, 1};
            manager->createContinuousFlow(flowId, flowDef, MXL_DATA_FORMAT_AUDIO, sampleRate, 8, sizeof(float), 8192);
        }());

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

TEST_CASE("Flow Manager : Create Audio Flow Structure", "[flow manager]")
{
    auto const domain = mxl::tests::getDomainPath();
    // Clean out the mxl domain path, if it exists.
    remove_all(domain);
    REQUIRE(create_directory(domain));

    auto const flowDef = mxl::tests::readFile("data/audio_flow.json");
    auto const flowId = *uuids::uuid::from_string("b3bb5be7-9fe9-4324-a5bb-4c70e1084449");
    auto const flowString = to_string(flowId);
    auto const sampleRate = Rational{48000, 1};

    auto manager = std::make_shared<FlowManager>(domain);
    auto flowData = manager->createContinuousFlow(flowId, flowDef, MXL_DATA_FORMAT_AUDIO, sampleRate, 2, sizeof(float), 4096);

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

    // This should throw since the flow metadata will already exist.
    REQUIRE_THROWS(
        [&]()
        {
            manager->createContinuousFlow(flowId, flowDef, MXL_DATA_FORMAT_AUDIO, sampleRate, 8, sizeof(float), 8192);
        }());

    // This should throw since the flow metadata will already exist.
    REQUIRE_THROWS(
        [&]()
        {
            auto const grainRate = Rational{60000, 1001};
            manager->createDiscreteFlow(flowId, flowDef, MXL_DATA_FORMAT_VIDEO, 5, grainRate, 1024);
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

TEST_CASE("Flow Manager : Open, List, and Error Conditions", "[flow manager]")
{
    auto const domain = mxl::tests::getDomainPath();
    // start clean
    auto ec = std::error_code{};
    remove_all(domain, ec);
    REQUIRE(std::filesystem::create_directory(domain));

    auto manager = std::make_shared<FlowManager>(domain);

    //
    // 1) Create & open a discrete flow
    //
    auto const flowId1 = *uuids::uuid::from_string("11111111-1111-1111-1111-111111111111");
    auto const flowDef1 = mxl::tests::readFile("data/v210_flow.json");
    auto const grainRate = Rational{60000, 1001};
    {
        auto flowData1 = manager->createDiscreteFlow(flowId1, flowDef1, MXL_DATA_FORMAT_VIDEO, 3, grainRate, 512);
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
    auto const sampleRate = Rational{48000, 1};
    {
        auto flowData2 = manager->createContinuousFlow(flowId2, flowDef2, MXL_DATA_FORMAT_AUDIO, sampleRate, 4, sizeof(float), 2048);
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
        REQUIRE(std::filesystem::create_directory(invalidDir));
        manager = std::make_shared<FlowManager>(domain);
        auto flows2 = manager->listFlows();
        REQUIRE(flows2.empty());
    }

    //
    // 9) listFlows on missing domain throws
    //
    remove_all(domain, ec);
    REQUIRE_THROWS_AS(manager->listFlows(), std::filesystem::filesystem_error);

    //
    // 10) unsupported formats should be rejected
    //
    auto const badId = *uuids::uuid::from_string("44444444-4444-4444-4444-444444444444");
    REQUIRE_THROWS_AS(manager->createDiscreteFlow(badId, flowDef1, MXL_DATA_FORMAT_UNSPECIFIED, 1, grainRate, 128), std::runtime_error);
    REQUIRE_THROWS_AS(manager->createContinuousFlow(badId, flowDef2, MXL_DATA_FORMAT_VIDEO, sampleRate, 1, 4, 1024), std::runtime_error);
}

// Re-creation after deletion
TEST_CASE("FlowManager: re-create flow after deletion", "[flow manager][reuse]")
{
    auto domain = makeTempDomain();
    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = Rational{50, 1};

    // Create discrete flow #1
    auto f1 = mgr->createDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, 128);
    REQUIRE(f1);
    REQUIRE(mgr->listFlows().size() == 1);

    // Delete it
    REQUIRE(mgr->deleteFlow(id));
    REQUIRE(mgr->listFlows().empty());

    // Re-create with same ID
    auto f2 = mgr->createDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 2, rate, 256);
    REQUIRE(f2);
    REQUIRE(mgr->listFlows().size() == 1);

    // Cleanup
    remove_all(domain);
}

// Test robustness: corrupted flow directory with missing descriptor
TEST_CASE("FlowManager: skip corrupted flow with missing descriptor", "[flow manager][corruption]")
{
    auto domain = makeTempDomain();
    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = Rational{50, 1};

    // Create and publish
    auto flow = mgr->createDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, 64);
    flow.reset();
    REQUIRE(mgr->listFlows().size() == 1);

    // Manually remove the descriptor JSON to simulate corruption
    auto flowDir = makeFlowDirectoryName(domain, uuids::to_string(id));
    auto descFile = makeFlowDescriptorFilePath(flowDir);
    REQUIRE(std::filesystem::exists(descFile));
    std::filesystem::remove(descFile);

    // Current behavior: listFlows still reports the ID (only checks directory structure)
    // \todo : DESIGN DECISION NEEDED - Should corrupted flows be:
    //   a) Filtered out from listFlows() for safety, or
    //   b) Listed but marked as corrupted, or
    //   c) Listed normally (current behavior)?
    auto ids = mgr->listFlows();
    REQUIRE(ids.size() == 1);

    // Current behavior: openFlow works (shared-memory header contains metadata)
    // \todo : DESIGN DECISION NEEDED - Should openFlow:
    //   a) Succeed if shared memory is intact (current), or
    //   b) Fail without complete metadata files?
    auto rd = mgr->openFlow(id, AccessMode::READ_ONLY);
    REQUIRE(rd);
    auto* d = dynamic_cast<DiscreteFlowData*>(rd.get());
    REQUIRE(d);
    REQUIRE(d->grainCount() == 1U);

    remove_all(domain);
}

// Concurrent createDiscreteFlow with same UUID
TEST_CASE("FlowManager: concurrent createDiscreteFlow same UUID", "[flow manager][concurrency]")
{
    auto domain = makeTempDomain();
    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("cccccccc-cccc-cccc-cccc-cccccccccccc");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = Rational{50, 1};

    auto success = std::atomic<int>{0};
    auto failure = std::atomic<int>{0};
    auto worker = [&](int payloadSize)
    {
        try
        {
            auto f = mgr->createDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, payloadSize);
            if (f)
            {
                ++success;
            }
        }
        catch (...)
        {
            ++failure;
        }
    };

    auto t1 = std::thread{worker, 64};
    auto t2 = std::thread{worker, 128};
    t1.join();
    t2.join();

    // Exactly one must succeed, and one must fail
    REQUIRE(success.load() == 1);
    REQUIRE(failure.load() == 1);
    REQUIRE(mgr->listFlows().size() == 1);

    remove_all(domain);
}

// deleteFlow on read-only domain - current behavior
TEST_CASE("FlowManager: deleteFlow on read-only domain", "[flow manager][deletion]")
{
    auto domain = makeTempDomain();
    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("dddddddd-dddd-dddd-dddd-dddddddddddd");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = Rational{50, 1};

    auto f = mgr->createDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, 64);
    f.reset();
    REQUIRE(mgr->listFlows().size() == 1);

    // Make domain read-only
    std::filesystem::permissions(domain,
        std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all,
        std::filesystem::perm_options::remove);

    // DeleteFlow should respect filesystem permissions
    // When domain is read-only, deletion operations should fail gracefully
    // The method returns false and logs an error instead of throwing
    REQUIRE(mgr->deleteFlow(id) == false);

    // Restore full permissions for cleanup and verification
    std::filesystem::permissions(domain, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);

    // Since deletion failed, the flow should still exist
    REQUIRE(mgr->listFlows().size() == 1);
    remove_all(domain);
}

// Concurrent listFlows + deleteFlow does not crash
TEST_CASE("FlowManager: concurrent listFlows and deleteFlow", "[flow manager][concurrency]")
{
    auto domain = makeTempDomain();
    auto mgr = std::make_shared<FlowManager>(domain);
    auto ids = std::vector<uuids::uuid>{};
    for (int i = 0; i < 5; ++i)
    {
        // Last segment must be exactly 12 hex digits: 11 zeros + one hex digit
        auto id = *uuids::uuid::from_string(fmt::format("eeeeeeee-eeee-eeee-eeee-00000000000{:X}", i));
        ids.push_back(id);
        mgr->createDiscreteFlow(id, mxl::tests::readFile("data/v210_flow.json"), MXL_DATA_FORMAT_VIDEO, 1, Rational{50, 1}, 32);
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
    remove_all(domain);
}

// Multiple FlowManager instances on same domain
TEST_CASE("FlowManager: multiple instances share domain", "[flow manager]")
{
    auto domain = makeTempDomain();
    auto mgr1 = std::make_shared<FlowManager>(domain);
    auto mgr2 = std::make_shared<FlowManager>(domain);

    auto const id = *uuids::uuid::from_string("ffffffff-ffff-ffff-ffff-ffffffffffff");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = Rational{50, 1};

    auto f1 = mgr1->createDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, 16);
    REQUIRE(mgr1->listFlows().size() == 1);
    // mgr2 should see it too
    REQUIRE(mgr2->listFlows().size() == 1);

    // Delete via mgr2
    REQUIRE(mgr2->deleteFlow(id));
    REQUIRE(mgr1->listFlows().empty());
    REQUIRE(mgr2->listFlows().empty());

    remove_all(domain);
}

// createFlow on unwritable domain - current behavior
TEST_CASE("FlowManager: createFlow throws on unwritable domain", "[flow manager][error]")
{
    auto domain = makeTempDomain();
    // remove write perms on domain
    std::filesystem::permissions(domain, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);

    auto mgr = std::make_shared<FlowManager>(domain);
    auto const id = *uuids::uuid::from_string("aaaaaaaa-0000-0000-0000-000000000000");
    auto const def = mxl::tests::readFile("data/v210_flow.json");
    auto const rate = Rational{50, 1};

    // CreateFlow should respect filesystem permissions
    // When domain is unwritable, creation operations should fail
    // This test verifies proper permission enforcement
    REQUIRE_THROWS_AS(mgr->createDiscreteFlow(id, def, MXL_DATA_FORMAT_VIDEO, 1, rate, 8), std::filesystem::filesystem_error);

    // restore perms so we can clean up
    std::filesystem::permissions(domain, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
    remove_all(domain);
}