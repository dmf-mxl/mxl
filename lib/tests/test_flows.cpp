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
#include <picojson/wrapper.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "../internal/include/mxl-internal/MediaUtils.hpp"

namespace fs = std::filesystem;

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Video Flow : Create/Destroy", "[mxl flows]")
{
    auto const opts = "{}";
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    bool active = false;

    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), "", &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

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

    // Confirm that the grain index is set in the grain info
    REQUIRE(gInfo.index == index);

    // Confirm that the grain size and stride lengths are what we expect.
    constexpr auto w = 1920;
    constexpr auto h = 1080;

    auto fillPayloadStrideSize = mxl::lib::getV210LineLength(w);
    REQUIRE(configInfo.discrete.sliceSizes[0] == fillPayloadStrideSize);
    REQUIRE(configInfo.discrete.sliceSizes[1] == 0);
    REQUIRE(configInfo.discrete.sliceSizes[2] == 0);
    REQUIRE(configInfo.discrete.sliceSizes[3] == 0);

    auto fillPayloadSize = fillPayloadStrideSize * h;
    REQUIRE(gInfo.grainSize == fillPayloadSize);

    /// Set a mark at the beginning and the end of the grain payload.
    buffer[0] = 0xCA;
    buffer[gInfo.grainSize - 1] = 0xFE;

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    mxlFlowRuntimeInfo runtimeInfo1;
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo1) == MXL_STATUS_OK);
    REQUIRE(runtimeInfo1.headIndex == 0);

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
    mxlFlowRuntimeInfo runtimeInfo2;
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo2) == MXL_STATUS_OK);

    /// Confirm that that head has moved.
    REQUIRE(runtimeInfo2.headIndex == index);

    // We accessed the grain using mxlFlowReaderGetGrain. This should have increased the lastReadTime field.
    REQUIRE(runtimeInfo2.lastReadTime > runtimeInfo1.lastReadTime);

    // We commited a new grain. This should have increased the lastWriteTime field.
    REQUIRE(runtimeInfo2.lastWriteTime > runtimeInfo1.lastWriteTime);

    /// Release the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);

    // Use the writer after closing the reader.
    buffer = nullptr;
    REQUIRE(mxlFlowWriterOpenGrain(writer, index++, &gInfo, &buffer) == MXL_STATUS_OK);
    /// Set a mark at the beginning and the end of the grain payload.
    buffer[0] = 0xCA;
    buffer[gInfo.grainSize - 1] = 0xFE;

    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Video Flow (With Alpha) : Create/Destroy", "[mxl flows]")
{
    auto const opts = "{}";
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    auto flowDef = mxl::tests::readFile("data/v210a_flow.json");
    bool active = false;

    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), "", &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

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

    // Confirm that the grain size and stride lengths are what we expect.
    constexpr auto w = 1920;
    constexpr auto h = 1080;

    auto fillPayloadStrideSize = mxl::lib::getV210LineLength(w);
    auto fillPayloadSize = fillPayloadStrideSize * h;
    REQUIRE(configInfo.discrete.sliceSizes[0] == fillPayloadStrideSize);

    auto keyPayloadStrideSize = static_cast<std::size_t>((w + 2) / 3 * 4);
    auto keyPayloadSize = keyPayloadStrideSize * h;
    REQUIRE(configInfo.discrete.sliceSizes[1] == keyPayloadStrideSize);
    REQUIRE(configInfo.discrete.sliceSizes[2] == 0);
    REQUIRE(configInfo.discrete.sliceSizes[3] == 0);

    REQUIRE(gInfo.grainSize == (fillPayloadSize + keyPayloadSize));

    /// Set a mark at the beginning and the end of the grain payload.
    buffer[0] = 0xCA;
    buffer[gInfo.grainSize - 1] = 0xFE;

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    mxlFlowRuntimeInfo runtimeInfo1;
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo1) == MXL_STATUS_OK);
    REQUIRE(runtimeInfo1.headIndex == 0);

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
    mxlFlowRuntimeInfo runtimeInfo2;
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo2) == MXL_STATUS_OK);

    /// Confirm that that head has moved.
    REQUIRE(runtimeInfo2.headIndex == index);

    // We accessed the grain using mxlFlowReaderGetGrain. This should have increased the lastReadTime field.
    REQUIRE(runtimeInfo2.lastReadTime > runtimeInfo1.lastReadTime);

    // We commited a new grain. This should have increased the lastWriteTime field.
    REQUIRE(runtimeInfo2.lastWriteTime > runtimeInfo1.lastWriteTime);

    /// Release the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);

    // Use the writer after closing the reader.
    buffer = nullptr;
    REQUIRE(mxlFlowWriterOpenGrain(writer, index++, &gInfo, &buffer) == MXL_STATUS_OK);
    /// Set a mark at the beginning and the end of the grain payload.
    buffer[0] = 0xCA;
    buffer[gInfo.grainSize - 1] = 0xFE;

    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Video Flow : Invalid flow (discrete)", "[mxl flows]")
{
    auto const opts = "{}";
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";
    auto flowDef = mxl::tests::readFile("data/v210_flow.json");

    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), "", &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    // The writer is now created. The flow should be active.
    bool active = false;
    REQUIRE(mxlIsFlowActive(instanceReader, flowId, &active) == MXL_STATUS_OK);
    REQUIRE(active == true);

    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);

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

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Invalid flow definitions", "[mxl flows]")
{
    // Create the instance
    char const* opts = "{}";
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

    mxlFlowConfigInfo configInfo;
    mxlFlowWriter writer;

    // Create a flow definition with no grain rate
    auto noGrainRateObj = validFlowObj;
    noGrainRateObj.erase("grain_rate");
    auto const noGrainRate = picojson::value(noGrainRateObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instance, noGrainRate.c_str(), opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create a flow definition with no id
    auto noIdObj = validFlowObj;
    noIdObj.erase("id");
    auto const noId = picojson::value(noIdObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instance, noId.c_str(), opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create a flow definition with no media type
    auto noMediaTypeObj = validFlowObj;
    noMediaTypeObj.erase("media_type");
    auto const noMediaType = picojson::value(noMediaTypeObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instance, noMediaType.c_str(), opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create a flow definition without label
    auto labelObj = validFlowObj;
    labelObj.erase("label");
    auto const noLabel = picojson::value(labelObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instance, noLabel.c_str(), opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create an invalid flow definition with an empty label
    labelObj["label"] = picojson::value{""};
    auto const emptyLabel = picojson::value(labelObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instance, emptyLabel.c_str(), opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create a flow definition with an invalid tag
    auto invalidTagObj = validFlowObj;
    auto& tagObj = invalidTagObj.find("tags")->second.get<picojson::object>();
    auto& tagArray = tagObj["urn:x-nmos:tag:grouphint/v1.0"].get<picojson::array>();
    tagArray.push_back(picojson::value{"a/b/c"});
    auto const invalidTag = picojson::value(invalidTagObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instance, invalidTag.c_str(), opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create a flow definition without tags
    auto noTagsObj = validFlowObj;
    noTagsObj.erase("tags");
    auto const noTags = picojson::value(noTagsObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instance, noTags.c_str(), opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create an interlaced flow definition with an invalid grain rate
    auto invalidInterlacedRateObj = validFlowObj;
    invalidInterlacedRateObj["interlace_mode"] = picojson::value{"interlaced_tff"};
    auto& rate = invalidInterlacedRateObj.find("grain_rate")->second.get<picojson::object>();
    rate["numerator"] = picojson::value{60000.0};
    auto const invalidInterlaced = picojson::value(invalidInterlacedRateObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instance, invalidInterlaced.c_str(), opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create an interlaced flow definition with an invalid height
    auto invalidInterlacedHeightObj = validFlowObj;
    invalidInterlacedHeightObj["interlace_mode"] = picojson::value{"interlaced_tff"};
    invalidInterlacedHeightObj["frame_height"] = picojson::value{1081.0};
    auto const invalidInterlacedHeight = picojson::value(invalidInterlacedHeightObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instance, invalidInterlacedHeight.c_str(), opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create a flow definition that is not json
    char const* malformed = "{ this is not json";
    REQUIRE(mxlCreateFlowWriter(instance, malformed, opts, &writer, &configInfo, nullptr) != MXL_STATUS_OK);

    // Create a flow definition that has a non-normalized grain rate. it creating the flow
    // should succeed but the grain rate should be normalized when we read the flow info back.
    {
        bool flowWasCreated = false;
        auto nonNormalizedRateObj = validFlowObj;
        auto& rate = nonNormalizedRateObj.find("grain_rate")->second.get<picojson::object>();
        // This is a dumb way to express 50/1.
        rate["numerator"] = picojson::value{100000.0};
        rate["denominator"] = picojson::value{2000.0};
        auto const nonNormalizedRate = picojson::value(nonNormalizedRateObj).serialize();
        REQUIRE(mxlCreateFlowWriter(instance, nonNormalizedRate.c_str(), opts, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
        REQUIRE(flowWasCreated);

        // the rational value found in the json should be normalized to 50/1.
        REQUIRE(configInfo.common.grainRate.numerator == 50);
        REQUIRE(configInfo.common.grainRate.denominator == 1);
        REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    }

    mxlDestroyInstance(instance);
}

#ifndef __APPLE__

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Data Flow : Create/Destroy", "[mxl flows]")
{
    fs::path domain{"/dev/shm/mxl_domain"}; // Remove that path if it exists.
    fs::remove_all(domain);
    std::filesystem::create_directories(domain);

    // Read some RFC-8331 packets from a pcap file downloaded from https://github.com/NEOAdvancedTechnology/ST2110_pcap_zoo
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
    REQUIRE(rtpSize > 14);
    rtpData += 14; // skip packet header until Length field, as defined in RFC-8331, section 2.
    rtpSize -= 14;
    uint8_t ancCount = rtpData[2];
    REQUIRE(ancCount == 1);

    char const* opts = "{}";
    auto flowDef = mxl::tests::readFile("data/data_flow.json");
    char const* flowId = "db3bd465-2772-484f-8fac-830b0471258b";

    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), "", &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

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

    // Copy the RFC-8331 packet in the grain
    memcpy(buffer, rtpData, rtpSize);

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    mxlFlowRuntimeInfo runtimeInfo1;
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo1) == MXL_STATUS_OK);
    REQUIRE(runtimeInfo1.headIndex == 0);

    /// Mark the grain as invalid
    gInfo.flags |= MXL_GRAIN_FLAG_INVALID;
    REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);

    /// Read back the grain using a flow reader.
    REQUIRE(mxlFlowReaderGetGrain(reader, index, 16, &gInfo, &buffer) == MXL_STATUS_OK);

    /// Confirm that the flags are preserved.
    REQUIRE(gInfo.flags == MXL_GRAIN_FLAG_INVALID);

    /// Confirm that our original RFC-8331 packet is still there
    REQUIRE(0 == memcmp(buffer, rtpData, reinterpret_cast<size_t>(rtpSize)));

    // Give some time to the inotify message to reach the directorywatcher.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    /// Get the updated flow info
    mxlFlowRuntimeInfo runtimeInfo2;
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo2) == MXL_STATUS_OK);

    /// Confirm that that head has moved.
    REQUIRE(runtimeInfo2.headIndex == index);

    // We accessed the grain using mxlFlowReaderGetGrain. This should have increased the lastReadTime field.
    REQUIRE(runtimeInfo2.lastReadTime > runtimeInfo1.lastReadTime);

    // We commited a new grain. This should have increased the lastWriteTime field.
    REQUIRE(runtimeInfo2.lastWriteTime > runtimeInfo1.lastWriteTime);

    /// Delete the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);

    // Use the writer after closing the reader.
    uint8_t* buffer2 = nullptr;
    REQUIRE(mxlFlowWriterOpenGrain(writer, index++, &gInfo, &buffer2) == MXL_STATUS_OK);

    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Video Flow : Options", "[mxl flows]")
{
    auto flowDef = mxl::tests::readFile("data/v210_flow.json");

    auto instanceOpts = "";
    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), instanceOpts);
    REQUIRE(instanceWriter != nullptr);

    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;

    picojson::object optsObj;
    optsObj["maxCommitBatchSizeHint"] = picojson::value{0.0}; // Invalid value

    auto optsStr = picojson::value(optsObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), optsStr.c_str(), &writer, &configInfo, nullptr) == MXL_ERR_UNKNOWN);

    optsObj.clear();
    optsObj["maxSyncBatchSizeHint"] = picojson::value{-1.0}; // Invalid value
    optsStr = picojson::value(optsObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), optsStr.c_str(), &writer, &configInfo, nullptr) == MXL_ERR_UNKNOWN);

    optsObj.clear();
    optsObj["maxCommitBatchSizeHint"] = picojson::value{5.0};
    optsObj["maxSyncBatchSizeHint"] = picojson::value{6.0}; // Not a multiple of maxCommitBatchSizeHint
    optsStr = picojson::value(optsObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), optsStr.c_str(), &writer, &configInfo, nullptr) == MXL_ERR_UNKNOWN);

    optsObj.clear();
    optsObj["maxCommitBatchSizeHint"] = picojson::value{5.0};
    optsObj["maxSyncBatchSizeHint"] = picojson::value{10.0};
    optsStr = picojson::value(optsObj).serialize();
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), optsStr.c_str(), &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    REQUIRE(configInfo.common.maxCommitBatchSizeHint == 5U);
    REQUIRE(configInfo.common.maxSyncBatchSizeHint == 10U);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);

    flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), nullptr, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    REQUIRE(configInfo.common.maxCommitBatchSizeHint == 1080U);
    REQUIRE(configInfo.common.maxSyncBatchSizeHint == 1080U);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceWriter) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Video Flow : Slices", "[mxl flows]")
{
    char const* opts = "{}";
    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    char const* flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    mxlFlowConfigInfo configInfo;
    mxlFlowWriter writer;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), opts, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

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
    mxlFlowInfo flowInfo1;
    REQUIRE(mxlFlowReaderGetInfo(reader, &flowInfo1) == MXL_STATUS_OK);
    REQUIRE(flowInfo1.runtime.headIndex == 0);

    // Total number of batches that will be written
    auto const numBatches =
        (gInfo.totalSlices + flowInfo1.config.common.maxCommitBatchSizeHint - 1U) / flowInfo1.config.common.maxCommitBatchSizeHint;
    auto defaultBatchSize = std::size_t{flowInfo1.config.common.maxCommitBatchSizeHint};

    for (auto batchIndex = std::size_t{0}; batchIndex < numBatches; batchIndex++)
    {
        auto batchSize = defaultBatchSize;

        // If the total number of slices is not a multiple of the default batch size, the last batch must
        // be the number of remaining slices
        if ((batchIndex + 1) * batchSize > gInfo.totalSlices)
        {
            batchSize = gInfo.totalSlices - gInfo.validSlices;
        }

        /// Write a slice to the grain.
        gInfo.validSlices += batchSize;
        REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);

        mxlFlowRuntimeInfo sliceRuntimeInfo;
        REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &sliceRuntimeInfo) == MXL_STATUS_OK);
        REQUIRE(sliceRuntimeInfo.headIndex == index);

        // We commited data to a grain. This should have increased the lastWriteTime field.
        REQUIRE(sliceRuntimeInfo.lastWriteTime > flowInfo1.runtime.lastWriteTime);

        /// Read back the partial grain using the flow reader.
        std::uint8_t* readBuffer = nullptr;
        auto const expectedValidSlices = std::min(defaultBatchSize * (batchIndex + 1), static_cast<std::size_t>(gInfo.totalSlices));
        REQUIRE(mxlFlowReaderGetGrainSlice(reader, index, expectedValidSlices, 8, &gInfo, &readBuffer) == MXL_STATUS_OK);

        // Validate the commited size
        REQUIRE(gInfo.validSlices == expectedValidSlices);

        // Give some time to the inotify message to reach the directorywatcher.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // We accessed the grain using mxlFlowReaderGetGrain. This should have increased the lastReadTime field.
        REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &sliceRuntimeInfo) == MXL_STATUS_OK);
        REQUIRE(sliceRuntimeInfo.lastReadTime > flowInfo1.runtime.lastReadTime);
    }

    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceReader) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceWriter) == MXL_STATUS_OK);
}

#endif

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Audio Flow : Create/Destroy", "[mxl flows]")
{
    auto const opts = "{}";
    auto const flowId = "b3bb5be7-9fe9-4324-a5bb-4c70e1084449";
    auto const flowDef = mxl::tests::readFile("data/audio_flow.json");

    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    mxlFlowWriter writer;
    {
        mxlFlowConfigInfo configInfo;
        bool flowWasCreated = false;
        REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), opts, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
        REQUIRE(flowWasCreated);

        REQUIRE(configInfo.common.grainRate.numerator == 48000U);
        REQUIRE(configInfo.common.grainRate.denominator == 1U);
        REQUIRE(configInfo.continuous.channelCount == 2U);
        REQUIRE(configInfo.continuous.bufferLength > 128U);
    }

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

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
        REQUIRE(payloadBuffersSlices.count == 2U);
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
        mxlFlowRuntimeInfo runtimeInfo;
        REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo) == MXL_STATUS_OK);

        // Verify that the headindex is yet to be modified
        REQUIRE(runtimeInfo.headIndex == 0);

        /// Commit the sample range
        REQUIRE(mxlFlowWriterCommitSamples(writer) == MXL_STATUS_OK);
    }

    {
        /// Open a range of samples for reading
        mxlWrappedMultiBufferSlice payloadBuffersSlices;
        REQUIRE(mxlFlowReaderGetSamplesNonBlocking(reader, index, 64U, &payloadBuffersSlices) == MXL_STATUS_OK);

        // Verify that the returned info looks alright
        REQUIRE(payloadBuffersSlices.count == 2U);
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
        mxlFlowRuntimeInfo runtimeInfo;
        REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtimeInfo) == MXL_STATUS_OK);

        // Confirm that that head has moved.
        REQUIRE(runtimeInfo.headIndex == index);
    }

    /// Release the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);

    {
        // Use the writer after closing the reader.
        mxlMutableWrappedMultiBufferSlice payloadBuffersSlices;
        REQUIRE(mxlFlowWriterOpenSamples(writer, index + 64U, 64U, &payloadBuffersSlices) == MXL_STATUS_OK);

        // Verify that the returned info looks alright
        REQUIRE(payloadBuffersSlices.count == 2U);
        REQUIRE((payloadBuffersSlices.base.fragments[0].size + payloadBuffersSlices.base.fragments[1].size) == 256U);
    }

    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);

    mxlDestroyInstance(instanceReader);
    mxlDestroyInstance(instanceWriter);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Audio Flow : Invalid Flow (continuous)", "[mxl flows]")
{
    auto const opts = "{}";
    auto const flowId = "b3bb5be7-9fe9-4324-a5bb-4c70e1084449";
    auto const flowDef = mxl::tests::readFile("data/audio_flow.json");

    auto instanceReader = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceReader != nullptr);

    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instanceWriter != nullptr);

    mxlFlowWriter writer;
    {
        mxlFlowConfigInfo configInfo;
        bool flowWasCreated = false;
        REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), opts, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
        REQUIRE(flowWasCreated);

        REQUIRE(configInfo.common.grainRate.numerator == 48000U);
        REQUIRE(configInfo.common.grainRate.denominator == 1U);
        REQUIRE(configInfo.continuous.channelCount == 2U);
        REQUIRE(configInfo.continuous.bufferLength > 128U);
    }

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instanceReader, flowId, "", &reader) == MXL_STATUS_OK);

    // This should be gone from the filesystem.
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);

    /// Compute the grain index for the flow rate and current TAI time.
    auto const rate = mxlRational{48000, 1};
    auto const now = mxlGetTime();
    auto const index = mxlTimestampToIndex(&rate, now);
    REQUIRE(index != MXL_UNDEFINED_INDEX);

    // Recreate the flow with the same id.
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), opts, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);

    {
        /// Open a range of samples for reading.
        /// Since we never commited to the stale stream this read attempt should
        /// come early, thus triggering the validity check, which in turn should
        /// detect the stream to be stale.
        mxlWrappedMultiBufferSlice payloadBuffersSlices;
        REQUIRE(mxlFlowReaderGetSamplesNonBlocking(reader, index, 64U, &payloadBuffersSlices) == MXL_ERR_FLOW_INVALID);
    }

    /// Release the reader
    REQUIRE(mxlReleaseFlowReader(instanceReader, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);

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

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Audio Flow : Different writer / reader batch size", "[mxl flows]")
{
    auto const opts = "{}";
    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("data/audio_flow.json");
    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), opts, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    auto const flowId = uuids::to_string(configInfo.common.id);
    REQUIRE(
        configInfo.continuous.bufferLength > 11U); // To have at least 2 samples per batch in our second part of the test with reading in 3 batches.

    // We write the whole buffer worth of data in 4 batches, and then we try to read the second half back in both equally-sized batches and in
    // different-sized batches.
    auto const lastIndex = mxlGetCurrentIndex(&configInfo.common.grainRate);
    auto writeBatches = planAudioBatches(4, configInfo.continuous.bufferLength, lastIndex);

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

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId.c_str(), "", &reader) == MXL_STATUS_OK);
    auto const readCheckFn = [](mxlFlowReader reader, std::vector<BatchIndexAndSize> const& batches)
    {
        for (auto const& batch : batches)
        {
            mxlWrappedMultiBufferSlice payloadBuffersSlices;
            REQUIRE(mxlFlowReaderGetSamplesNonBlocking(reader, batch.index, batch.size, &payloadBuffersSlices) == MXL_STATUS_OK);
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
    // When checking the batches, we can only check the second half of the buffer (this is what mxlFlowReaderGetSamples and friends allow us).
    writeBatches.erase(writeBatches.begin(), writeBatches.begin() + writeBatches.size() / 2);
    readCheckFn(reader, writeBatches);
    auto const readBatches = planAudioBatches(writeBatches.size() + 1, configInfo.continuous.bufferLength / 2, lastIndex);
    readCheckFn(reader, readBatches);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);

    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Audio Flow : Options", "[mxl flows]")
{
    auto flowDef = mxl::tests::readFile("data/audio_flow.json");

    auto instanceOpts = "";
    auto instanceWriter = mxlCreateInstance(domain.string().c_str(), instanceOpts);
    REQUIRE(instanceWriter != nullptr);

    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;

    picojson::object optsObj;
    optsObj["maxCommitBatchSizeHint"] = picojson::value{-1.0}; // Invalid value

    auto optsStr = picojson::value(optsObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), optsStr.c_str(), &writer, &configInfo, nullptr) == MXL_ERR_UNKNOWN);

    optsObj.clear();
    optsObj["maxSyncBatchSizeHint"] = picojson::value{0.0}; // Invalid value
    optsStr = picojson::value(optsObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), optsStr.c_str(), &writer, &configInfo, nullptr) == MXL_ERR_UNKNOWN);

    optsObj.clear();
    optsObj["maxCommitBatchSizeHint"] = picojson::value{10.0};
    optsObj["maxSyncBatchSizeHint"] = picojson::value{2.0}; // Not a multiple of maxCommitBatchSizeHint
    optsStr = picojson::value(optsObj).serialize();
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), optsStr.c_str(), &writer, &configInfo, nullptr) == MXL_ERR_UNKNOWN);

    optsObj.clear();
    optsObj["maxCommitBatchSizeHint"] = picojson::value{5.0};
    optsObj["maxSyncBatchSizeHint"] = picojson::value{10.0};
    optsStr = picojson::value(optsObj).serialize();
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), optsStr.c_str(), &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    REQUIRE(configInfo.common.maxCommitBatchSizeHint == 5U);
    REQUIRE(configInfo.common.maxSyncBatchSizeHint == 10U);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);

    flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instanceWriter, flowDef.c_str(), nullptr, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    REQUIRE(configInfo.common.maxCommitBatchSizeHint == 480U);
    REQUIRE(configInfo.common.maxSyncBatchSizeHint == 480U);
    REQUIRE(mxlReleaseFlowWriter(instanceWriter, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instanceWriter) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "mxlGetFlowDef", "[mxl flows]")
{
    auto const opts = "{}";
    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), opts, &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    auto const flowId = uuids::to_string(configInfo.common.id);

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

    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

// Verify that we obtain a proper error code when attempting to create a flow
// in an unwritable domain.
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "mxlCreateFlow: unwritable domain", "[mxl flows]")
{
    // remove write perms on domain
    permissions(domain, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);

    auto const opts = "{}";
    auto instance = mxlCreateInstance(domain.string().c_str(), opts);
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), opts, &writer, &configInfo, nullptr) == MXL_ERR_PERMISSION_DENIED);

    // restore perms so we can clean up
    permissions(domain, std::filesystem::perms::owner_all, std::filesystem::perm_options::add);
    remove_all(domain);
}
