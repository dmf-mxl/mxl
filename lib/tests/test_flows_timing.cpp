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

/// The test simulates trying to read video data from the current head, while the writer is still
/// 3 grains behind (~100 ms at the 30000/1001 rate).
TEST_CASE("Video Flow : Wait for grain availability", "[mxl flows timing]")
{
    auto domain = std::filesystem::path{"./mxl_unittest_domain"};
    remove_all(domain);
    create_directories(domain);

    auto const opts = "{}";
    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    FlowInfo fInfo;
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &fInfo) == MXL_STATUS_OK);
    auto const flowId = uuids::to_string(fInfo.common.id);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId.c_str(), "", &reader) == MXL_STATUS_OK);
    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowId.c_str(), "", &writer) == MXL_STATUS_OK);

    auto const readerGrainIndex = mxlGetCurrentIndex(&fInfo.discrete.grainRate);
    auto const frameDurationNs = 1000000000 * fInfo.discrete.grainRate.denominator / fInfo.discrete.grainRate.numerator;
    auto writerThread = std::thread{[readerGrainIndex, frameDurationNs, writer]
        {
            constexpr auto writerLatencyGrains = 3;
            for (std::uint64_t writerGrainIndex = readerGrainIndex - writerLatencyGrains; writerGrainIndex <= readerGrainIndex; ++writerGrainIndex)
            {
                GrainInfo gInfo;
                std::uint8_t* buffer = nullptr;
                REQUIRE(mxlFlowWriterOpenGrain(writer, writerGrainIndex, &gInfo, &buffer) == MXL_STATUS_OK);
                REQUIRE(buffer != nullptr);
                memcpy(buffer, &writerGrainIndex, sizeof(std::uint64_t));
                gInfo.commitedSize = gInfo.grainSize;
                REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);
                if (writerGrainIndex < readerGrainIndex)
                {
                    mxlSleepForNs(frameDurationNs);
                }
            }
        }};

    GrainInfo gInfo;
    std::uint8_t* buffer = nullptr;
    constexpr auto oneS = 1000000000;
    REQUIRE(mxlFlowReaderGetGrain(reader, readerGrainIndex, oneS, &gInfo, &buffer) == MXL_STATUS_OK);
    REQUIRE(buffer != nullptr);
    REQUIRE(gInfo.commitedSize == gInfo.grainSize);
    std::uint64_t obtainedGrainIndex = 0;
    memcpy(&obtainedGrainIndex, buffer, sizeof(std::uint64_t));
    REQUIRE(readerGrainIndex == obtainedGrainIndex);
    writerThread.join();

    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId.c_str()) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceReader) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceWriter) == MXL_STATUS_OK);
}
