#include "../src/internal/Time.hpp"
#include "Utils.hpp"

#ifndef __APPLE__
#   include <Device.h>
#   include <Packet.h>
#   include <PcapFileDevice.h>
#   include <ProtocolType.h>
#   include <RawPacket.h>
#   include <UdpLayer.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>

namespace fs = std::filesystem;

TEST_CASE("Video Flow : Create/Destroy", "[mxl flows]")
{
    auto const opts = "{}";
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    auto flowDef = mxl::tests::readFile("data/v210_flow.json");

    auto domain = std::filesystem::path{"./mxl_unittest_domain"}; // Remove that path if it exists.
    remove_all(domain);

    create_directories(domain);
    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    FlowInfo fInfo;
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &fInfo) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowId, "", &writer) == MXL_STATUS_OK);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = Rational{60000, 1001};
    auto const now = mxlGetTime();
    uint64_t index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    /// Open the grain.
    GrainInfo gInfo;
    uint8_t* buffer = nullptr;
    /// Open the grain for writing.
    REQUIRE(mxlFlowWriterOpenGrain(writer, index, &gInfo, &buffer) == MXL_STATUS_OK);

    /// Set a mark at the beginning and the end of the grain payload.
    buffer[0] = 0xCA;
    buffer[gInfo.grainSize - 1] = 0xFE;

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    FlowInfo fInfo1;
    REQUIRE(mxlFlowReaderGetInfo(reader, &fInfo1) == MXL_STATUS_OK);
    REQUIRE(fInfo1.discrete.headIndex == 0);

    /// Mark the grain as invalid
    gInfo.flags |= GRAIN_FLAG_INVALID;
    REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);

    /// Create back the grain using a flow reader.
    REQUIRE(mxlFlowReaderGetGrain(reader, index, 16, &gInfo, &buffer) == MXL_STATUS_OK);

    // Give some time to the inotify message to reach the directorywatcher.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    /// Confirm that the flags are preserved.
    REQUIRE(gInfo.flags == GRAIN_FLAG_INVALID);

    /// Confirm that the marks are still present.
    REQUIRE(buffer[0] == 0xCA);
    REQUIRE(buffer[gInfo.grainSize - 1] == 0xFE);

    /// Get the updated flow info
    FlowInfo fInfo2;
    REQUIRE(mxlFlowReaderGetInfo(reader, &fInfo2) == MXL_STATUS_OK);

    /// Confirm that that head has moved.
    REQUIRE(fInfo2.discrete.headIndex == index);

    // We accessed the grain using mxlFlowReaderGetGrain. This should have increased the lastReadTime field.
    REQUIRE(fInfo2.common.lastReadTime > fInfo1.common.lastReadTime);

    // We commited a new grain. This should have increased the lastWriteTime field.
    REQUIRE(fInfo2.common.lastWriteTime > fInfo1.common.lastWriteTime);

    /// Release the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);

    // Use the writer after closing the reader.
    buffer = nullptr;
    REQUIRE(mxlFlowWriterOpenGrain(writer, index++, &gInfo, &buffer) == MXL_STATUS_OK);
    /// Set a mark at the beginning and the end of the grain payload.
    buffer[0] = 0xCA;
    buffer[gInfo.grainSize - 1] = 0xFE;

    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_STATUS_OK);
    // This should be gone from the filesystem.
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_ERR_FLOW_NOT_FOUND);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}

TEST_CASE("Invalid flow definitions", "[mxl flows]")
{
    char const* opts = "{}";
    auto noGrainRate = mxl::tests::readFile("data/no_grain_rate.json");
    auto noId = mxl::tests::readFile("data/no_id.json");
    auto noMediaType = mxl::tests::readFile("data/no_media_type.json");
    auto malformed = mxl::tests::readFile("data/malformed.json");
    auto invalidInterlaced = mxl::tests::readFile("data/invalid_interlaced_rate.json");
    auto invalidInterlacedHeight = mxl::tests::readFile("data/invalid_interlaced_height.json");

    char const* homeDir = getenv("HOME");
    REQUIRE(homeDir != nullptr);
    fs::path domain{"./mxl_unittest_domain"}; // Remove that path if it exists.
    fs::remove_all(domain);

    std::filesystem::create_directories(domain);
    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    FlowInfo fInfo;
    REQUIRE(mxlCreateFlow(instance, noGrainRate.c_str(), opts, &fInfo) != MXL_STATUS_OK);
    REQUIRE(mxlCreateFlow(instance, noId.c_str(), opts, &fInfo) != MXL_STATUS_OK);
    REQUIRE(mxlCreateFlow(instance, noMediaType.c_str(), opts, &fInfo) != MXL_STATUS_OK);
    REQUIRE(mxlCreateFlow(instance, malformed.c_str(), opts, &fInfo) != MXL_STATUS_OK);
    REQUIRE(mxlCreateFlow(instance, invalidInterlaced.c_str(), opts, &fInfo) != MXL_STATUS_OK);
    REQUIRE(mxlCreateFlow(instance, invalidInterlacedHeight.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    mxlDestroyInstance(instance);
}

#ifndef __APPLE__

TEST_CASE("Data Flow : Create/Destroy", "[mxl flows]")
{
    // Read some 2110-40 packets from a pcap file downloaded from https://github.com/NEOAdvancedTechnology/ST2110_pcap_zoo
    std::unique_ptr<pcpp::IFileReaderDevice> pcapReader(pcpp::IFileReaderDevice::getReader("data/ST2110-40-Closed_Captions.cap"));
    REQUIRE(pcapReader != nullptr);
    REQUIRE(pcapReader->open());

    // We know that in the pcap file the first packet is an empty packet with a marker bit. Skip it and read the second one.
    pcpp::RawPacketVector rawPackets;
    REQUIRE(pcapReader->getNextPackets(rawPackets, 2) == 2);
    pcpp::Packet parsedPacket(rawPackets.at(1));

    auto udpLayer = parsedPacket.getLayerOfType<pcpp::UdpLayer>();
    auto* rtpData = udpLayer->getLayerPayload();
    REQUIRE(rtpData != nullptr);
    auto rtpSize = udpLayer->getLayerPayloadSize();
    REQUIRE(rtpSize > 12);
    rtpData += 12; // skip rtp headers.  hard coded. could be smarter.
    rtpSize -= 12;
    uint8_t ancCount = rtpData[4];
    REQUIRE(ancCount == 1);

    char const* opts = "{}";
    auto flowDef = mxl::tests::readFile("data/data_flow.json");
    char const* flowId = "db3bd465-2772-484f-8fac-830b0471258b";

    fs::path domain{"./mxl_unittest_domain"}; // Remove that path if it exists.
    fs::remove_all(domain);

    std::filesystem::create_directories(domain);
    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    FlowInfo fInfo;
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &fInfo) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowId, "", &writer) == MXL_STATUS_OK);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = Rational{60000, 1001};
    auto const now = mxlGetTime();
    uint64_t index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    /// Open the grain.
    GrainInfo gInfo;
    uint8_t* buffer = nullptr;
    /// Open the grain for writing.
    REQUIRE(mxlFlowWriterOpenGrain(writer, index, &gInfo, &buffer) == MXL_STATUS_OK);

    /// ANC Grains are always 4KiB
    REQUIRE(gInfo.grainSize == 4096);

    // Copy the 2110-40 packet in the grain
    memcpy(buffer, rtpData, rtpSize);

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    FlowInfo fInfo1;
    REQUIRE(mxlFlowReaderGetInfo(reader, &fInfo1) == MXL_STATUS_OK);
    REQUIRE(fInfo1.discrete.headIndex == 0);

    /// Mark the grain as invalid
    gInfo.flags |= GRAIN_FLAG_INVALID;
    REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);

    /// Create back the grain using a flow reader.
    REQUIRE(mxlFlowReaderGetGrain(reader, index, 16, &gInfo, &buffer) == MXL_STATUS_OK);

    /// Confirm that the flags are preserved.
    REQUIRE(gInfo.flags == GRAIN_FLAG_INVALID);

    /// Confirm that our original -40 packet is still there
    REQUIRE(0 == memcmp(buffer, rtpData, reinterpret_cast<size_t>(rtpSize)));

    // Give some time to the inotify message to reach the directorywatcher.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    /// Get the updated flow info
    FlowInfo fInfo2;
    REQUIRE(mxlFlowReaderGetInfo(reader, &fInfo2) == MXL_STATUS_OK);

    /// Confirm that that head has moved.
    REQUIRE(fInfo2.discrete.headIndex == index);

    // We accessed the grain using mxlFlowReaderGetGrain. This should have increased the lastReadTime field.
    REQUIRE(fInfo2.common.lastReadTime > fInfo1.common.lastReadTime);

    // We commited a new grain. This should have increased the lastWriteTime field.
    REQUIRE(fInfo2.common.lastWriteTime > fInfo1.common.lastWriteTime);

    /// Delete the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);

    // Use the writer after closing the reader.
    uint8_t* buffer2 = nullptr;
    REQUIRE(mxlFlowWriterOpenGrain(writer, index++, &gInfo, &buffer2) == MXL_STATUS_OK);

    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_STATUS_OK);
    // This should be gone from the filesystem.
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_ERR_FLOW_NOT_FOUND);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}

TEST_CASE("Video Flow : Slices", "[mxl flows]")
{
    char const* opts = "{}";
    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    char const* flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    fs::path domain{"/dev/shm/mxl_domain"}; // Remove that path if it exists.
    fs::remove_all(domain);

    std::filesystem::create_directories(domain);
    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    FlowInfo fInfo;
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &fInfo) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowId, "", &writer) == MXL_STATUS_OK);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = Rational{60000, 1001};
    auto const now = mxlGetTime();
    uint64_t index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    /// Open the grain.
    GrainInfo gInfo;
    uint8_t* buffer = nullptr;
    REQUIRE(mxlFlowWriterOpenGrain(writer, index, &gInfo, &buffer) == MXL_STATUS_OK);

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    FlowInfo fInfo1;
    REQUIRE(mxlFlowReaderGetInfo(reader, &fInfo1) == MXL_STATUS_OK);
    REQUIRE(fInfo1.discrete.headIndex == 0);

    size_t const maxSlice = 8;
    auto sliceSize = gInfo.grainSize / maxSlice;
    for (size_t slice = 0; slice < maxSlice; slice++)
    {
        /// Write a slice to the grain.
        gInfo.commitedSize += sliceSize;
        REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);

        FlowInfo sliceFlowInfo;
        REQUIRE(mxlFlowReaderGetInfo(reader, &sliceFlowInfo) == MXL_STATUS_OK);
        REQUIRE(sliceFlowInfo.discrete.headIndex == index);

        // We commited data to a grain. This should have increased the lastWriteTime field.
        REQUIRE(sliceFlowInfo.common.lastWriteTime > fInfo1.common.lastWriteTime);

        /// Read back the partial grain using the flow reader.
        REQUIRE(mxlFlowReaderGetGrain(reader, index, 8, &gInfo, &buffer) == MXL_STATUS_OK);

        // Validate the commited size
        REQUIRE(gInfo.commitedSize == sliceSize * (slice + 1));

        // Give some time to the inotify message to reach the directorywatcher.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // We accessed the grain using mxlFlowReaderGetGrain. This should have increased the lastReadTime field.
        REQUIRE(mxlFlowReaderGetInfo(reader, &sliceFlowInfo) == MXL_STATUS_OK);
        REQUIRE(sliceFlowInfo.common.lastReadTime > fInfo1.common.lastReadTime);
    }

    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_ERR_FLOW_NOT_FOUND);
    REQUIRE(mxlDestroyInstance(instanceReader) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceWriter) == MXL_STATUS_OK);
}

#endif

TEST_CASE("Audio Flow : Create/Destroy", "[mxl flows]")
{
    auto const opts = "{}";
    auto const flowId = "b3bb5be7-9fe9-4324-a5bb-4c70e1084449";
    auto const flowDef = mxl::tests::readFile("data/audio_flow.json");

    auto domain = std::filesystem::path{"./mxl_unittest_domain"}; // Remove that path if it exists.
    remove_all(domain);

    create_directories(domain);
    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    {
        FlowInfo flowInfo;
        REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &flowInfo) == MXL_STATUS_OK);

        REQUIRE(flowInfo.continuous.sampleRate.numerator == 48000U);
        REQUIRE(flowInfo.continuous.sampleRate.denominator == 1U);
        REQUIRE(flowInfo.continuous.channelCount == 1U);
        REQUIRE(flowInfo.continuous.bufferLength > 128U);
    }

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowId, "", &writer) == MXL_STATUS_OK);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = Rational{48000, 1};
    auto const now = mxlGetTime();
    auto const index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    {
        /// Open a range of samples for writing
        MutableWrappedMultiBufferSlice payloadBuffersSlices;
        REQUIRE(mxlFlowWriterOpenSamples(writer, index, 64U, &payloadBuffersSlices) == MXL_STATUS_OK);

        // Verify that the returned info looks alright
        REQUIRE(payloadBuffersSlices.count == 1U);
        REQUIRE((payloadBuffersSlices.base.fragments[0].size + payloadBuffersSlices.base.fragments[1].size) == 256U);

        // Fill some test data
        for (auto i = 0U; i < payloadBuffersSlices.base.fragments[0].size; ++i)
        {
            static_cast<std::uint8_t*>(payloadBuffersSlices.base.fragments[0].pointer)[i] = i;
        }
        for (auto i = 0U; i < payloadBuffersSlices.base.fragments[1].size; ++i)
        {
            static_cast<std::uint8_t*>(payloadBuffersSlices.base.fragments[0].pointer)[i] = payloadBuffersSlices.base.fragments[0].size + i;
        }

        /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
        FlowInfo flowInfo;
        REQUIRE(mxlFlowReaderGetInfo(reader, &flowInfo) == MXL_STATUS_OK);

        // Verify that the headindex is yet to be modified
        REQUIRE(flowInfo.continuous.headIndex == 0);

        /// Commit the sample range
        REQUIRE(mxlFlowWriterCommitSamples(writer) == MXL_STATUS_OK);
    }

    {
        /// Open a range of samples for reading
        WrappedMultiBufferSlice payloadBuffersSlices;
        REQUIRE(mxlFlowReaderGetSamples(reader, index, 64U, &payloadBuffersSlices) == MXL_STATUS_OK);

        // Verify that the returned info looks alright
        REQUIRE(payloadBuffersSlices.count == 1U);
        REQUIRE((payloadBuffersSlices.base.fragments[0].size + payloadBuffersSlices.base.fragments[1].size) == 256U);

        for (auto i = 0U; i < payloadBuffersSlices.base.fragments[0].size; ++i)
        {
            REQUIRE(static_cast<std::uint8_t const*>(payloadBuffersSlices.base.fragments[0].pointer)[i] == i);
        }
        for (auto i = 0U; i < payloadBuffersSlices.base.fragments[1].size; ++i)
        {
            REQUIRE(static_cast<std::uint8_t const*>(payloadBuffersSlices.base.fragments[0].pointer)[i] ==
                    payloadBuffersSlices.base.fragments[0].size + i);
        }

        // Get the updated flow info
        FlowInfo flowInfo;
        REQUIRE(mxlFlowReaderGetInfo(reader, &flowInfo) == MXL_STATUS_OK);

        // Confirm that that head has moved.
        REQUIRE(flowInfo.continuous.headIndex == index);
    }

    /// Release the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);

    {
        // Use the writer after closing the reader.
        MutableWrappedMultiBufferSlice payloadBuffersSlices;
        REQUIRE(mxlFlowWriterOpenSamples(writer, index + 64U, 64U, &payloadBuffersSlices) == MXL_STATUS_OK);

        // Verify that the returned info looks alright
        REQUIRE(payloadBuffersSlices.count == 1U);
        REQUIRE((payloadBuffersSlices.base.fragments[0].size + payloadBuffersSlices.base.fragments[1].size) == 256U);
    }

    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_STATUS_OK);

    // This should be gone from the filesystem.
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_ERR_FLOW_NOT_FOUND);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}