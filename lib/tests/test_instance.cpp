// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_instance.cpp
 * @brief Unit tests for MXL instance lifecycle and flow management
 *
 * This test suite validates MXL instance behavior including:
 *   - Flow reader/writer caching within instances
 *   - Multiple instances sharing the same domain
 *   - Flow lifecycle tied to writers (flow deletion when last writer releases)
 *   - Automatic flow cleanup on instance destruction
 *   - Reference counting for readers and writers
 *
 * Key concepts tested:
 *   - Instance isolation: Multiple instances can coexist in same domain
 *   - Flow ownership: Flows persist until all writers release them
 *   - Reader caching: Same flow ID returns same reader within an instance
 *   - Writer uniqueness: Each writer is distinct even for same flow
 *   - Automatic cleanup: Flows removed when last reference is released
 *
 * These tests use the mxlDomainFixture to ensure clean test isolation.
 */

#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "Utils.hpp"

/**
 * @brief Test that flow readers are cached within an instance
 *
 * This test verifies:
 *   - Creating multiple writers for different flows produces distinct writers
 *   - Creating multiple readers for the same flow returns the same cached reader
 *   - Readers and writers for the same flow are different objects
 *   - All readers/writers can be properly released
 *
 * Expected behavior:
 *   - mxlCreateFlowReader(instance, flowId, ...) returns same pointer on repeated calls
 *   - mxlCreateFlowWriter(instance, flowDef, ...) returns unique pointers
 *   - Reader and writer pointers differ even for same flow
 */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow readers / writers caching", "[instance]")
{
    auto const opts = "{}";
    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    mxlFlowWriter audioWriter;
    mxlFlowWriter videoWriter;
    mxlFlowWriter metaWriter;
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated;
    auto flowDef = mxl::tests::readFile("data/audio_flow.json");
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), opts, &audioWriter, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    auto const audioFlowId = uuids::to_string(configInfo.common.id);
    flowDef = mxl::tests::readFile("data/v210_flow.json");
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), opts, &videoWriter, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    auto const videoFlowId = uuids::to_string(configInfo.common.id);
    flowDef = mxl::tests::readFile("data/data_flow.json");
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), opts, &metaWriter, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    auto const metaFlowId = uuids::to_string(configInfo.common.id);

    REQUIRE(audioFlowId != videoFlowId);
    REQUIRE(audioFlowId != metaFlowId);
    REQUIRE(videoFlowId != metaFlowId);

    REQUIRE(audioWriter != videoWriter);
    REQUIRE(audioWriter != metaWriter);
    REQUIRE(videoWriter != metaWriter);

    mxlFlowReader audioReader;
    REQUIRE(mxlCreateFlowReader(instance, audioFlowId.c_str(), "", &audioReader) == MXL_STATUS_OK);
    REQUIRE(audioReader != nullptr);
    mxlFlowReader audioReader2;
    REQUIRE(mxlCreateFlowReader(instance, audioFlowId.c_str(), "", &audioReader2) == MXL_STATUS_OK);
    REQUIRE(audioReader2 != nullptr);
    mxlFlowReader videoReader;
    REQUIRE(mxlCreateFlowReader(instance, videoFlowId.c_str(), "", &videoReader) == MXL_STATUS_OK);
    REQUIRE(videoReader != nullptr);
    mxlFlowReader metaReader;
    REQUIRE(mxlCreateFlowReader(instance, metaFlowId.c_str(), "", &metaReader) == MXL_STATUS_OK);
    REQUIRE(metaReader != nullptr);

    REQUIRE(audioReader == audioReader2);
    REQUIRE(audioReader != videoReader);
    REQUIRE(audioReader != metaReader);
    REQUIRE(videoReader != metaReader);

    REQUIRE(static_cast<void*>(audioReader) != static_cast<void*>(audioWriter));
    REQUIRE(static_cast<void*>(audioReader) != static_cast<void*>(videoWriter));
    REQUIRE(static_cast<void*>(audioReader) != static_cast<void*>(metaWriter));

    REQUIRE(mxlReleaseFlowReader(instance, audioReader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, audioReader2) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, videoReader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, metaReader) == MXL_STATUS_OK);

    REQUIRE(mxlReleaseFlowWriter(instance, audioWriter) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, videoWriter) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, metaWriter) == MXL_STATUS_OK);

    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

/**
 * @brief Test that flows persist until all writers release them
 *
 * This test verifies reference counting behavior:
 *   - Two instances create writers for the same flow
 *   - First writer release: flow still exists (second writer holds reference)
 *   - Second writer release: flow is deleted (no more references)
 *
 * Expected behavior:
 *   - Flow directory exists as long as at least one writer exists
 *   - Flow directory is removed when the last writer releases
 *   - Multiple instances can safely share the same flow
 *
 * This prevents premature flow deletion when multiple writers are active.
 */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow deletion on writer release", "[instance]")
{
    auto instanceA = mxlCreateInstance(domain.string().c_str(), nullptr);
    auto instanceB = mxlCreateInstance(domain.string().c_str(), nullptr);

    mxlFlowWriter writerA = nullptr;
    mxlFlowWriter writerB = nullptr;
    mxlFlowConfigInfo flowConfig;
    bool flowWasCreated = false;

    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    REQUIRE(mxlCreateFlowWriter(instanceA, flowDef.c_str(), nullptr, &writerA, &flowConfig, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    REQUIRE(mxlCreateFlowWriter(instanceB, flowDef.c_str(), nullptr, &writerB, &flowConfig, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE_FALSE(flowWasCreated);

    auto id = uuids::to_string(flowConfig.common.id);

    // The flow directory should exists after the flow has been created
    REQUIRE(flowDirectoryExists(id));

    // The flow directory should still exists after the first writer has been released
    mxlReleaseFlowWriter(instanceA, writerA);
    REQUIRE(flowDirectoryExists(id));

    // The flow directory should have been removed after the second writer has been released
    mxlReleaseFlowWriter(instanceB, writerB);
    REQUIRE_FALSE(flowDirectoryExists(id));

    REQUIRE(mxlDestroyInstance(instanceA) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceB) == MXL_STATUS_OK);
}

/**
 * @brief Test that flow cleanup occurs on instance destruction
 *
 * This test verifies automatic cleanup behavior:
 *   - Two instances create writers for the same flow (without explicit release)
 *   - First instance destruction: flow still exists (second instance has writer)
 *   - Second instance destruction: flow is deleted (all instances gone)
 *
 * Expected behavior:
 *   - Destroying an instance implicitly releases its flow writers
 *   - Flow persists as long as any instance has an active writer
 *   - Last instance destruction triggers flow cleanup
 *
 * This ensures proper cleanup even when writers aren't explicitly released
 * before instance destruction (e.g., in case of errors or early returns).
 */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Flow deletion on instance destruction", "[instance]")
{
    auto instanceA = mxlCreateInstance(domain.string().c_str(), nullptr);
    auto instanceB = mxlCreateInstance(domain.string().c_str(), nullptr);

    mxlFlowWriter writerA = nullptr;
    mxlFlowWriter writerB = nullptr;
    mxlFlowConfigInfo flowConfig;
    bool flowWasCreated = false;

    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    REQUIRE(mxlCreateFlowWriter(instanceA, flowDef.c_str(), nullptr, &writerA, &flowConfig, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    REQUIRE(mxlCreateFlowWriter(instanceB, flowDef.c_str(), nullptr, &writerB, &flowConfig, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE_FALSE(flowWasCreated);

    auto id = uuids::to_string(flowConfig.common.id);

    REQUIRE(flowDirectoryExists(id));

    mxlDestroyInstance(instanceA);
    REQUIRE(flowDirectoryExists(id));

    mxlDestroyInstance(instanceB);
    REQUIRE_FALSE(flowDirectoryExists(id));
}
