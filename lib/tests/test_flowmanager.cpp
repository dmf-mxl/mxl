// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include "../src/internal/FlowManager.hpp"
#include "../src/internal/PathUtils.hpp"
#include "Utils.hpp"

using namespace mxl::lib;

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

    auto grainCount = 0U;
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
