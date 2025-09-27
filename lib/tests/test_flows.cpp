// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#ifndef __APPLE__
#   include <Device.h>
#   include <Packet.h>
#   include <PcapFileDevice.h>
#   include <ProtocolType.h>
#   include <RawPacket.h>
#   include <UdpLayer.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <thread>
#include <uuid.h>
#include <catch2/catch_test_macros.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <picojson/picojson.h>

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

    mxlFlowInfo fInfo;
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &fInfo) == MXL_STATUS_OK);

    // We created the flow but it does not have a writer yet. The flow should not be active.
    bool active = true;
    REQUIRE(mxlIsFlowActive(instanceReader, flowId, &active) == MXL_STATUS_OK);
    REQUIRE(active == false);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowId, "", &writer) == MXL_STATUS_OK);

    // The writer is now created. The flow should be active.
    REQUIRE(mxlIsFlowActive(instanceReader, flowId, &active) == MXL_STATUS_OK);
    REQUIRE(active == true);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = mxlRational{60000, 1001};
    auto const now = mxlGetTime();
    uint64_t index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    /// Open the grain.
    mxlGrainInfo gInfo;
    uint8_t* buffer = nullptr;
    /// Open the grain for writing.
    REQUIRE(mxlFlowWriterOpenGrain(writer, index, &gInfo, &buffer) == MXL_STATUS_OK);

    /// Set a mark at the beginning and the end of the grain payload.
    buffer[0] = 0xCA;
    buffer[gInfo.grainSize - 1] = 0xFE;

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    mxlFlowInfo fInfo1;
    REQUIRE(mxlFlowReaderGetInfo(reader, &fInfo1) == MXL_STATUS_OK);
    REQUIRE(fInfo1.discrete.headIndex == 0);

    /// Mark the grain as invalid
    gInfo.flags |= MXL_GRAIN_FLAG_INVALID;
    REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);

    /// Read back the grain using a flow reader.
    REQUIRE(mxlFlowReaderGetGrain(reader, index, 16, &gInfo, &buffer) == MXL_STATUS_OK);

    // Give some time to the inotify message to reach the directorywatcher.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    /// Confirm that the flags are preserved.
    REQUIRE(gInfo.flags == MXL_GRAIN_FLAG_INVALID);

    /// Confirm that the marks are still present.
    REQUIRE(buffer[0] == 0xCA);
    REQUIRE(buffer[gInfo.grainSize - 1] == 0xFE);

    /// Get the updated flow info
    mxlFlowInfo fInfo2;
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

    // The writer is now gone. The flow should be inactive.
    REQUIRE(mxlIsFlowActive(instanceReader, flowId, &active) == MXL_STATUS_OK);
    REQUIRE(active == false);

    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_STATUS_OK);
    // This should be gone from the filesystem.
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_ERR_FLOW_NOT_FOUND);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}

TEST_CASE("Video Flow : Invalid flow (discrete)", "[mxl flows]")
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

    mxlFlowInfo fInfo;
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &fInfo) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowId, "", &writer) == MXL_STATUS_OK);

    // The writer is now created. The flow should be active.
    bool active = false;
    REQUIRE(mxlIsFlowActive(instanceReader, flowId, &active) == MXL_STATUS_OK);
    REQUIRE(active == true);

    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_STATUS_OK);
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &fInfo) == MXL_STATUS_OK);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = mxlRational{60000, 1001};
    auto const now = mxlGetTime();
    uint64_t index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    /// Open the grain.
    mxlGrainInfo gInfo;
    uint8_t* buffer = nullptr;

    REQUIRE(mxlFlowReaderGetGrain(reader, index, 16, &gInfo, &buffer) == MXL_ERR_FLOW_INVALID);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}

TEST_CASE("Invalid flow definitions", "[mxl flows]")
{
    //
    // Prepare the domain
    //
    char const* homeDir = getenv("HOME");
    REQUIRE(homeDir != nullptr);
    fs::path domain{"./mxl_unittest_domain"}; // Remove that path if it exists.
    fs::remove_all(domain);

    // Create the instance
    char const* opts = "{}";
    std::filesystem::create_directories(domain);
    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    //
    // Parse a valid flow definition and keep it as a reference picojson object.
    //
    auto const flowDef = mxl::tests::readFile("data/v210_flow.json");
    auto jsonValue = picojson::value{};
    auto const err = picojson::parse(jsonValue, flowDef);
    REQUIRE(err.empty());
    REQUIRE(jsonValue.is<picojson::object>());
    auto const validFlowObj = jsonValue.get<picojson::object>();

    mxlFlowInfo fInfo;

    // Create a flow definition with no grain rate
    auto noGrainRateObj = validFlowObj;
    noGrainRateObj.erase("grain_rate");
    auto const noGrainRate = picojson::value(noGrainRateObj).serialize();
    REQUIRE(mxlCreateFlow(instance, noGrainRate.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    // Create a flow definition with no id
    auto noIdObj = validFlowObj;
    noIdObj.erase("id");
    auto const noId = picojson::value(noIdObj).serialize();
    REQUIRE(mxlCreateFlow(instance, noId.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    // Create a flow definition with no media type
    auto noMediaTypeObj = validFlowObj;
    noMediaTypeObj.erase("media_type");
    auto const noMediaType = picojson::value(noMediaTypeObj).serialize();
    REQUIRE(mxlCreateFlow(instance, noMediaType.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    // Create a flow definition without label
    auto labelObj = validFlowObj;
    labelObj.erase("label");
    auto const noLabel = picojson::value(labelObj).serialize();
    REQUIRE(mxlCreateFlow(instance, noLabel.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    // Create an invalid flow definition with an empty label
    labelObj["label"] = picojson::value{""};
    auto const emptyLabel = picojson::value(labelObj).serialize();
    REQUIRE(mxlCreateFlow(instance, emptyLabel.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    // Create a flow definition with an invalid tag
    auto invalidTagObj = validFlowObj;
    auto& tagObj = invalidTagObj.find("tags")->second.get<picojson::object>();
    auto& tagArray = tagObj["urn:x-nmos:tag:grouphint/v1.0"].get<picojson::array>();
    tagArray.push_back(picojson::value{"a/b/c"});
    auto const invalidTag = picojson::value(invalidTagObj).serialize();
    REQUIRE(mxlCreateFlow(instance, invalidTag.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    // Create a flow definition without tags
    auto noTagsObj = validFlowObj;
    noTagsObj.erase("tags");
    auto const noTags = picojson::value(noTagsObj).serialize();
    REQUIRE(mxlCreateFlow(instance, noTags.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    // Create an interlaced flow definition with an invalid grain rate
    auto invalidInterlacedRateObj = validFlowObj;
    invalidInterlacedRateObj["interlace_mode"] = picojson::value{"interlaced_tff"};
    auto& rate = invalidInterlacedRateObj.find("grain_rate")->second.get<picojson::object>();
    rate["numerator"] = picojson::value{60000.0};
    auto const invalidInterlaced = picojson::value(invalidInterlacedRateObj).serialize();
    REQUIRE(mxlCreateFlow(instance, invalidInterlaced.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    // Create an interlaced flow definition with an invalid height
    auto invalidInterlacedHeightObj = validFlowObj;
    invalidInterlacedHeightObj["interlace_mode"] = picojson::value{"interlaced_tff"};
    invalidInterlacedHeightObj["frame_height"] = picojson::value{1081.0};
    auto const invalidInterlacedHeight = picojson::value(invalidInterlacedHeightObj).serialize();
    REQUIRE(mxlCreateFlow(instance, invalidInterlacedHeight.c_str(), opts, &fInfo) != MXL_STATUS_OK);

    // Create a flow definition that is not json
    char const* malformed = "{ this is not json";
    REQUIRE(mxlCreateFlow(instance, malformed, opts, &fInfo) != MXL_STATUS_OK);

    // Create a flow definition that has a non-normalized grain rate. it creating the flow
    // should succeed but the grain rate should be normalized when we read the flow info back.
    {
        auto nonNormalizedRateObj = validFlowObj;
        auto& rate = nonNormalizedRateObj.find("grain_rate")->second.get<picojson::object>();
        // This is a dumb way to express 50/1.
        rate["numerator"] = picojson::value{100000.0};
        rate["denominator"] = picojson::value{2000.0};
        auto const nonNormalizedRate = picojson::value(nonNormalizedRateObj).serialize();
        REQUIRE(mxlCreateFlow(instance, nonNormalizedRate.c_str(), opts, &fInfo) == MXL_STATUS_OK);

        // the rational value found in the json should be normalized to 50/1.
        REQUIRE(fInfo.discrete.grainRate.numerator == 50);
        REQUIRE(fInfo.discrete.grainRate.denominator == 1);
        REQUIRE(mxlDestroyFlow(instance, "5fbec3b1-1b0f-417d-9059-8b94a47197ed") == MXL_STATUS_OK);
    }

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

    mxlFlowInfo fInfo;
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &fInfo) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowId, "", &writer) == MXL_STATUS_OK);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = mxlRational{60000, 1001};
    auto const now = mxlGetTime();
    uint64_t index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    /// Open the grain.
    mxlGrainInfo gInfo;
    uint8_t* buffer = nullptr;
    /// Open the grain for writing.
    REQUIRE(mxlFlowWriterOpenGrain(writer, index, &gInfo, &buffer) == MXL_STATUS_OK);

    /// ANC Grains are always 4KiB
    REQUIRE(gInfo.grainSize == 4096);

    // Copy the 2110-40 packet in the grain
    memcpy(buffer, rtpData, rtpSize);

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    mxlFlowInfo fInfo1;
    REQUIRE(mxlFlowReaderGetInfo(reader, &fInfo1) == MXL_STATUS_OK);
    REQUIRE(fInfo1.discrete.headIndex == 0);

    /// Mark the grain as invalid
    gInfo.flags |= MXL_GRAIN_FLAG_INVALID;
    REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);

    /// Read back the grain using a flow reader.
    REQUIRE(mxlFlowReaderGetGrain(reader, index, 16, &gInfo, &buffer) == MXL_STATUS_OK);

    /// Confirm that the flags are preserved.
    REQUIRE(gInfo.flags == MXL_GRAIN_FLAG_INVALID);

    /// Confirm that our original -40 packet is still there
    REQUIRE(0 == memcmp(buffer, rtpData, reinterpret_cast<size_t>(rtpSize)));

    // Give some time to the inotify message to reach the directorywatcher.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    /// Get the updated flow info
    mxlFlowInfo fInfo2;
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

    mxlFlowInfo fInfo;
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &fInfo) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowId, "", &writer) == MXL_STATUS_OK);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = mxlRational{60000, 1001};
    auto const now = mxlGetTime();
    uint64_t index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    /// Open the grain.
    mxlGrainInfo gInfo;
    uint8_t* buffer = nullptr;
    REQUIRE(mxlFlowWriterOpenGrain(writer, index, &gInfo, &buffer) == MXL_STATUS_OK);

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    mxlFlowInfo fInfo1;
    REQUIRE(mxlFlowReaderGetInfo(reader, &fInfo1) == MXL_STATUS_OK);
    REQUIRE(fInfo1.discrete.headIndex == 0);

    size_t const maxSlice = 8;
    auto sliceSize = gInfo.grainSize / maxSlice;
    for (size_t slice = 0; slice < maxSlice; slice++)
    {
        /// Write a slice to the grain.
        gInfo.commitedSize += sliceSize;
        REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);

        mxlFlowInfo sliceFlowInfo;
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
        mxlFlowInfo flowInfo;
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
    auto const rate = mxlRational{48000, 1};
    auto const now = mxlGetTime();
    auto const index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    {
        /// Open a range of samples for writing
        mxlMutableWrappedMultiBufferSlice payloadBuffersSlices;
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
            static_cast<std::uint8_t*>(payloadBuffersSlices.base.fragments[1].pointer)[i] = payloadBuffersSlices.base.fragments[0].size + i;
        }

        /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
        mxlFlowInfo flowInfo;
        REQUIRE(mxlFlowReaderGetInfo(reader, &flowInfo) == MXL_STATUS_OK);

        // Verify that the headindex is yet to be modified
        REQUIRE(flowInfo.continuous.headIndex == 0);

        /// Commit the sample range
        REQUIRE(mxlFlowWriterCommitSamples(writer) == MXL_STATUS_OK);
    }

    {
        /// Open a range of samples for reading
        mxlWrappedMultiBufferSlice payloadBuffersSlices;
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
            REQUIRE(static_cast<std::uint8_t const*>(payloadBuffersSlices.base.fragments[1].pointer)[i] ==
                    payloadBuffersSlices.base.fragments[0].size + i);
        }

        // Get the updated flow info
        mxlFlowInfo flowInfo;
        REQUIRE(mxlFlowReaderGetInfo(reader, &flowInfo) == MXL_STATUS_OK);

        // Confirm that that head has moved.
        REQUIRE(flowInfo.continuous.headIndex == index);
    }

    /// Release the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);

    {
        // Use the writer after closing the reader.
        mxlMutableWrappedMultiBufferSlice payloadBuffersSlices;
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

TEST_CASE("Audio Flow : Invalid Flow (continuous)", "[mxl flows]")
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
        mxlFlowInfo flowInfo;
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

    // This should be gone from the filesystem.
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_STATUS_OK);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = mxlRational{48000, 1};
    auto const now = mxlGetTime();
    auto const index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    // Recreate the flow with the same id.
    mxlFlowInfo flowInfo;
    REQUIRE(mxlCreateFlow(instanceWriter, flowDef.c_str(), opts, &flowInfo) == MXL_STATUS_OK);

    {
        /// Open a range of samples for reading. This should detect that the flow is invalid.
        mxlWrappedMultiBufferSlice payloadBuffersSlices;
        REQUIRE(mxlFlowReaderGetSamples(reader, index, 64U, &payloadBuffersSlices) == MXL_ERR_FLOW_INVALID);
    }

    /// Release the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyFlow(instanceWriter, flowId) == MXL_STATUS_OK);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}

struct BatchIndexAndSize
{
    std::uint64_t index;
    std::size_t size;
};

/// Prepares reading or writing batches in a way that the given numOfSamples are split into numOfBatches batches, which can be read or written. The
/// batch with the lowest index (containing the "oldest" data) is the first one.
std::vector<BatchIndexAndSize> planAudioBatches(std::size_t numOfBatches, std::size_t numOfSamples, std::uint64_t lastBatchIndex)
{
    std::vector<BatchIndexAndSize> result;
    auto const batchSize = numOfSamples / numOfBatches;
    auto const remainder = numOfSamples % numOfBatches;

    std::size_t samplesSoFar = 0U;
    for (std::size_t i = 0; i < numOfBatches; ++i)
    {
        BatchIndexAndSize batch;
        batch.size = batchSize + (i < remainder ? 1 : 0);
        samplesSoFar += batch.size;
        batch.index = lastBatchIndex - numOfSamples + samplesSoFar;
        result.push_back(batch);
    }

    return result;
}

TEST_CASE("Audio Flow : Different writer / reader batch size", "[mxl flows]")
{
    auto domain = std::filesystem::path{"./mxl_unittest_domain"};
    remove_all(domain);
    create_directories(domain);

    auto const opts = "{}";
    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("data/audio_flow.json");
    mxlFlowInfo flowInfo;
    REQUIRE(mxlCreateFlow(instance, flowDef.c_str(), opts, &flowInfo) == MXL_STATUS_OK);
    auto const flowId = uuids::to_string(flowInfo.common.id);
    REQUIRE(flowInfo.continuous.bufferLength > 11U); // To have at least 2 samples per batch in our second part of the test with reading in 3 batches.

    // We write the whole buffer worth of data in 4 batches, and then we try to read the second half back in both equally-sized batches and in
    // different-sized batches.
    auto const lastIndex = mxlGetCurrentIndex(&flowInfo.continuous.sampleRate);
    auto writeBatches = planAudioBatches(4, flowInfo.continuous.bufferLength, lastIndex);

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowId.c_str(), "", &writer) == MXL_STATUS_OK);
    for (auto const& batch : writeBatches)
    {
        mxlMutableWrappedMultiBufferSlice payloadBuffersSlices;
        REQUIRE(mxlFlowWriterOpenSamples(writer, batch.index, batch.size, &payloadBuffersSlices) == MXL_STATUS_OK);
        REQUIRE((payloadBuffersSlices.base.fragments[0].size + payloadBuffersSlices.base.fragments[1].size) / 4 == batch.size);
        std::uint64_t index = batch.index - batch.size + 1;
        for (std::size_t i = 0U; i < payloadBuffersSlices.base.fragments[0].size / 4; ++i)
        {
            static_cast<std::uint32_t*>(payloadBuffersSlices.base.fragments[0].pointer)[i] = static_cast<std::uint32_t>(index++);
        }
        for (std::size_t i = 0U; i < payloadBuffersSlices.base.fragments[1].size / 4; ++i)
        {
            static_cast<std::uint32_t*>(payloadBuffersSlices.base.fragments[1].pointer)[i] = static_cast<std::uint32_t>(index++);
        }
        REQUIRE(index == batch.index + 1);
        REQUIRE(mxlFlowWriterCommitSamples(writer) == MXL_STATUS_OK);
    }
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId.c_str(), "", &reader) == MXL_STATUS_OK);
    auto const readCheckFn = [](mxlFlowReader reader, std::vector<BatchIndexAndSize> const& batches)
    {
        for (auto const& batch : batches)
        {
            mxlWrappedMultiBufferSlice payloadBuffersSlices;
            REQUIRE(mxlFlowReaderGetSamples(reader, batch.index, batch.size, &payloadBuffersSlices) == MXL_STATUS_OK);
            REQUIRE((payloadBuffersSlices.base.fragments[0].size + payloadBuffersSlices.base.fragments[1].size) / 4 == batch.size);
            std::uint64_t index = batch.index - batch.size + 1;
            for (std::size_t i = 0U; i < payloadBuffersSlices.base.fragments[0].size / 4; ++i)
            {
                REQUIRE(static_cast<std::uint32_t const*>(payloadBuffersSlices.base.fragments[0].pointer)[i] == static_cast<std::uint32_t>(index++));
            }
            for (std::size_t i = 0U; i < payloadBuffersSlices.base.fragments[1].size / 4; ++i)
            {
                REQUIRE(static_cast<std::uint32_t const*>(payloadBuffersSlices.base.fragments[1].pointer)[i] == static_cast<std::uint32_t>(index++));
            }
            REQUIRE(index == batch.index + 1);
        }
    };
    // When checking the batches, we can only check the second half of the buffer (this is what mxlFlowReaderGetSamples allows us).
    writeBatches.erase(writeBatches.begin(), writeBatches.begin() + writeBatches.size() / 2);
    readCheckFn(reader, writeBatches);
    auto const readBatches = planAudioBatches(writeBatches.size() + 1, flowInfo.continuous.bufferLength / 2, lastIndex);
    readCheckFn(reader, readBatches);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);

    REQUIRE(mxlDestroyFlow(instance, flowId.c_str()) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE("mxlGetFlowDef", "[mxl flows]")
{
    auto domain = std::filesystem::path{"./mxl_unittest_domain"};
    remove_all(domain);
    create_directories(domain);

    auto const opts = "{}";
    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    mxlFlowInfo flowInfo;
    REQUIRE(mxlCreateFlow(instance, flowDef.c_str(), opts, &flowInfo) == MXL_STATUS_OK);
    auto const flowId = uuids::to_string(flowInfo.common.id);

    char fourKBuffer[4096];
    auto fourKBufferSize = sizeof(fourKBuffer);

    REQUIRE(mxlGetFlowDef(nullptr, flowId.c_str(), fourKBuffer, &fourKBufferSize) == MXL_ERR_INVALID_ARG);
    REQUIRE(fourKBufferSize == sizeof(fourKBuffer));

    REQUIRE(mxlGetFlowDef(instance, nullptr, fourKBuffer, &fourKBufferSize) == MXL_ERR_INVALID_ARG);
    REQUIRE(fourKBufferSize == sizeof(fourKBuffer));
    REQUIRE(mxlGetFlowDef(instance, "this is not UUID", fourKBuffer, &fourKBufferSize) == MXL_ERR_INVALID_ARG);
    REQUIRE(fourKBufferSize == sizeof(fourKBuffer));
    REQUIRE(mxlGetFlowDef(instance, "75f369f9-6814-48a3-b827-942bc24c3d25", fourKBuffer, &fourKBufferSize) == MXL_ERR_FLOW_NOT_FOUND);
    REQUIRE(fourKBufferSize == sizeof(fourKBuffer));

    REQUIRE(mxlGetFlowDef(instance, flowId.c_str(), fourKBuffer, nullptr) == MXL_ERR_INVALID_ARG);

    auto requiredBufferSize = size_t{0U};
    REQUIRE(mxlGetFlowDef(instance, flowId.c_str(), nullptr, &requiredBufferSize) == MXL_ERR_INVALID_ARG);
    REQUIRE(requiredBufferSize == flowDef.size() + 1);
    auto requiredBufferSize2 = size_t{10U};
    REQUIRE(mxlGetFlowDef(instance, flowId.c_str(), fourKBuffer, &requiredBufferSize2) == MXL_ERR_INVALID_ARG);
    REQUIRE(requiredBufferSize == requiredBufferSize2);

    requiredBufferSize = fourKBufferSize;
    REQUIRE(mxlGetFlowDef(instance, flowId.c_str(), fourKBuffer, &requiredBufferSize) == MXL_STATUS_OK);
    REQUIRE(requiredBufferSize == requiredBufferSize2);
    REQUIRE(flowDef == std::string{fourKBuffer});

    REQUIRE(mxlDestroyFlow(instance, flowId.c_str()) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

// Verify that we obtain a proper error code when attempting to create a flow
// in an unwritable domain.
TEST_CASE("mxlCreateFlow: unwritable domain", "[mxl flows]")
{
    auto domain = mxl::tests::makeTempDomain();
    // remove write perms on domain
    permissions(domain, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);

    auto const opts = "{}";
    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    mxlFlowInfo flowInfo;
    REQUIRE(mxlCreateFlow(instance, flowDef.c_str(), opts, &flowInfo) == MXL_ERR_PERMISSION_DENIED);

    // restore perms so we can clean up
    permissions(domain, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
    remove_all(domain);
}