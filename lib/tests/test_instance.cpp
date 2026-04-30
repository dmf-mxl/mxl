// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "Utils.hpp"

#ifdef __linux__
#   include <sys/vfs.h>
#   include <linux/magic.h>

namespace
{
    bool pathIsTmpFs(char const* path)
    {
        struct statfs buf;
        REQUIRE(statfs(path, &buf) == 0);
        return (buf.f_type == TMPFS_MAGIC) || (buf.f_type == RAMFS_MAGIC);
    }
}
#endif

TEST_CASE("mxlIsTmpFs returns MXL_ERR_INVALID_ARG for NULL path", "[mxlIsTmpFs]")
{
    bool isTmpFs = true;
    REQUIRE(mxlIsTmpFs(nullptr, &isTmpFs) == MXL_ERR_INVALID_ARG);
}

TEST_CASE("mxlIsTmpFs returns MXL_ERR_INVALID_ARG for NULL out pointer", "[mxlIsTmpFs]")
{
    REQUIRE(mxlIsTmpFs("/tmp", nullptr) == MXL_ERR_INVALID_ARG);
}

TEST_CASE("mxlIsTmpFs returns error for non-existent path", "[mxlIsTmpFs]")
{
    bool isTmpFs = true;
    REQUIRE(mxlIsTmpFs("/this/path/does/not/exist", &isTmpFs) != MXL_STATUS_OK);
}

#ifdef __linux__
TEST_CASE("mxlIsTmpFs matches statfs filesystem type on Linux", "[mxlIsTmpFs]")
{
    bool isTmpFs = false;
    REQUIRE(mxlIsTmpFs("/", &isTmpFs) == MXL_STATUS_OK);
    REQUIRE(isTmpFs == pathIsTmpFs("/"));
}

TEST_CASE("mxlIsTmpFs detects /dev/shm as tmpfs on Linux", "[mxlIsTmpFs]")
{
    bool isTmpFs = false;
    REQUIRE(mxlIsTmpFs("/dev/shm", &isTmpFs) == MXL_STATUS_OK);
    REQUIRE(isTmpFs);
}
#endif

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
