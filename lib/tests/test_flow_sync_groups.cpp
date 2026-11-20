// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdint>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "Utils.hpp"

namespace
{
    constexpr auto VIDEO_FLOW_ID = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    constexpr auto DATA_FLOW_ID = "db3bd465-2772-484f-8fac-830b0471258b";

    /// Both test flows share this grain rate, so a given timestamp maps to the same
    /// grain index and expected arrival time on either of them.
    constexpr auto GRAIN_RATE = mxlRational{30000, 1001};

    /// Commit the grain with the given index after `delay`, so that a reader waiting on
    /// it observes a source delay of at least `delay`.
    mxlStatus commitGrainAfter(mxlFlowWriter writer, std::uint64_t index, std::chrono::milliseconds delay)
    {
        std::this_thread::sleep_for(delay);

        mxlGrainInfo info;
        std::uint8_t* buffer = nullptr;
        if (auto const status = mxlFlowWriterOpenGrain(writer, index, &info, &buffer); status != MXL_STATUS_OK)
        {
            return status;
        }
        info.validSlices = info.totalSlices;
        return mxlFlowWriterCommitGrain(writer, &info);
    }
}

///
/// A synchronization group must keep synchronizing for as long as it exists.
///
/// `FlowSynchronizationGroup::waitForDataAt` opportunistically moves the flow with the
/// largest observed source delay to the front of its reader list. This test makes the
/// *second* member the slower one, so that the reordering is triggered, and then checks
/// that the group still waits on its members afterwards.
///
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Synchronization group : Repeated waits", "[mxl sync groups]")
{
    auto const opts = "{}";

    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    // Two discrete flows sharing a grain rate. The data flow is written late on purpose.
    mxlFlowWriter videoWriter;
    mxlFlowWriter dataWriter;
    mxlFlowConfigInfo configInfo;
    bool created = false;
    REQUIRE(
        mxlCreateFlowWriter(instance, mxl::tests::readFile("data/v210_flow.json").c_str(), "", &videoWriter, &configInfo, &created) == MXL_STATUS_OK);
    REQUIRE(
        mxlCreateFlowWriter(instance, mxl::tests::readFile("data/data_flow.json").c_str(), "", &dataWriter, &configInfo, &created) == MXL_STATUS_OK);

    mxlFlowReader videoReader;
    mxlFlowReader dataReader;
    REQUIRE(mxlCreateFlowReader(instance, VIDEO_FLOW_ID, "", &videoReader) == MXL_STATUS_OK);
    REQUIRE(mxlCreateFlowReader(instance, DATA_FLOW_ID, "", &dataReader) == MXL_STATUS_OK);

    mxlFlowSynchronizationGroup group;
    REQUIRE(mxlCreateFlowSynchronizationGroup(instance, &group) == MXL_STATUS_OK);
    REQUIRE(mxlFlowSynchronizationGroupAddReader(group, videoReader) == MXL_STATUS_OK);
    REQUIRE(mxlFlowSynchronizationGroupAddReader(group, dataReader) == MXL_STATUS_OK);

    // First wait: both grains arrive, the second member later than the first.
    auto const firstIndex = mxlTimestampToIndex(&GRAIN_RATE, mxlGetTime()) + 1;
    auto const firstTimestamp = mxlIndexToTimestamp(&GRAIN_RATE, firstIndex);

    auto videoStatus = MXL_ERR_UNKNOWN;
    auto dataStatus = MXL_ERR_UNKNOWN;
    auto videoThread = std::thread{[&]
        {
            videoStatus = commitGrainAfter(videoWriter, firstIndex, std::chrono::milliseconds{50});
        }};
    auto dataThread = std::thread{[&]
        {
            dataStatus = commitGrainAfter(dataWriter, firstIndex, std::chrono::milliseconds{250});
        }};

    auto const firstWaitStatus = mxlFlowSynchronizationGroupWaitForDataAt(group, firstTimestamp, 2'000'000'000ULL);

    videoThread.join();
    dataThread.join();

    REQUIRE(videoStatus == MXL_STATUS_OK);
    REQUIRE(dataStatus == MXL_STATUS_OK);
    REQUIRE(firstWaitStatus == MXL_STATUS_OK);

    // Second wait, on the same group, for a grain that nobody will ever write. The group
    // must block until the timeout expires instead of reporting the data as available.
    auto const missingTimestamp = mxlIndexToTimestamp(&GRAIN_RATE, firstIndex + 1000);

    auto const start = std::chrono::steady_clock::now();
    auto const secondWaitStatus = mxlFlowSynchronizationGroupWaitForDataAt(group, missingTimestamp, 200'000'000ULL);
    auto const elapsed = std::chrono::steady_clock::now() - start;

    CHECK(secondWaitStatus != MXL_STATUS_OK);
    CHECK(elapsed >= std::chrono::milliseconds{150});

    REQUIRE(mxlReleaseFlowSynchronizationGroup(instance, group) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, videoReader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, dataReader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, videoWriter) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, dataWriter) == MXL_STATUS_OK);
    mxlDestroyInstance(instance);
}
