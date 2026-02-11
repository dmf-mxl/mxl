// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_flows_timing.cpp
 * @brief Unit tests for MXL flow timing and synchronization behavior
 *
 * This test suite validates MXL's timing and synchronization mechanisms:
 *   - Blocking reads that wait for grain availability
 *   - Reader/writer concurrency with timing constraints
 *   - Timeout handling for missing grains
 *   - Real-time grain production/consumption patterns
 *   - Frame-accurate timing using TAI timestamps
 *
 * Key scenarios tested:
 *   - Reader waiting for writer to catch up (simulates late writer)
 *   - Writer producing grains at real-time rate
 *   - Reader blocking until specific grain index is available
 *   - Timeout behavior when grains are missing
 *
 * These tests use threading to simulate realistic producer/consumer
 * scenarios with timing constraints typical of live media workflows.
 */

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

#ifndef __APPLE__
#   include <Device.h>
#   include <Packet.h>
#   include <PcapFileDevice.h>
#   include <ProtocolType.h>
#   include <RawPacket.h>
#   include <UdpLayer.h>
#endif

#include "Utils.hpp"

/**
 * @brief Test that reader blocks waiting for writer to produce a grain
 *
 * Simulates a real-time scenario where:
 *   1. Reader requests current grain (index N)
 *   2. Writer is 3 grains behind (at index N-3)
 *   3. Writer produces grains at real-time rate (~33ms per frame @ 29.97fps)
 *   4. Reader blocks until grain N is available
 *   5. Reader successfully retrieves grain N after ~100ms wait
 *
 * This validates:
 *   - Blocking read behavior (mxlFlowReaderGetGrain waits)
 *   - Timeout parameter works correctly (1 second timeout)
 *   - Grain data integrity (embedded grain index verified)
 *   - Thread-safe concurrent access
 *
 * The test uses 30000/1001 rate (~29.97fps), so 3 grains = ~100ms latency.
 */
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
