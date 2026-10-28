// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <thread>
#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "Utils.hpp"

/// The test simulates trying to read video data from the current head, while the writer is still
/// 3 grains behind (~100 ms at the 30000/1001 rate).
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Video Flow : Wait for grain availability", "[mxl flows timing]")
{
    auto const opts = "{}";
    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), opts, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);

    mxlFlowReader reader;
    auto const flowId = uuids::to_string(configInfo.common.id);
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId.c_str(), "", &reader) == MXL_STATUS_OK);

    auto const readerGrainIndex = mxlGetCurrentIndex(&configInfo.common.grainRate);
    auto const frameDurationNs = 1000000000 * configInfo.common.grainRate.denominator / configInfo.common.grainRate.numerator;
    auto writerThread = std::thread{[readerGrainIndex, frameDurationNs, writer]
        {
            constexpr auto writerLatencyGrains = 3;
            for (std::uint64_t writerGrainIndex = readerGrainIndex - writerLatencyGrains; writerGrainIndex <= readerGrainIndex; ++writerGrainIndex)
            {
                mxlGrainInfo gInfo;
                std::uint8_t* buffer = nullptr;
                REQUIRE(mxlFlowWriterOpenGrain(writer, writerGrainIndex, &gInfo, &buffer) == MXL_STATUS_OK);
                REQUIRE(buffer != nullptr);
                memcpy(buffer, &writerGrainIndex, sizeof(std::uint64_t));
                gInfo.validSlices = gInfo.totalSlices;
                REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);
                if (writerGrainIndex < readerGrainIndex)
                {
                    mxlSleepForNs(frameDurationNs);
                }
            }
        }};

    mxlGrainInfo gInfo;
    std::uint8_t* buffer = nullptr;
    constexpr auto oneS = 1000000000;
    REQUIRE(mxlFlowReaderGetGrain(reader, readerGrainIndex, oneS, &gInfo, &buffer) == MXL_STATUS_OK);
    REQUIRE(buffer != nullptr);
    REQUIRE(gInfo.validSlices == gInfo.totalSlices);
    std::uint64_t obtainedGrainIndex = 0;
    memcpy(&obtainedGrainIndex, buffer, sizeof(std::uint64_t));
    REQUIRE(readerGrainIndex == obtainedGrainIndex);
    writerThread.join();

    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceReader) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceWriter) == MXL_STATUS_OK);
}
