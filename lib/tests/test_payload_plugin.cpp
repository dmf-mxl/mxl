// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstring>
#include <filesystem>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include <picojson/picojson.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "Utils.hpp"

namespace fs = std::filesystem;

#ifndef MXL_TEST_PAYLOAD_PLUGIN_PATH
#   error "MXL_TEST_PAYLOAD_PLUGIN_PATH is not set"
#endif

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Video Flow : dlopen payload plugin", "[mxl flows][payload-plugin]")
{
    auto const options = std::string{"{\"payloadPlugins\":[\""} + MXL_TEST_PAYLOAD_PLUGIN_PATH + "\"]}";
    auto instance = mxlCreateInstance(domain.string().c_str(), options.c_str());
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("data/v210_flow.json");
    picojson::object payload;
    payload["location"] = picojson::value{"device"};
    payload["deviceIndex"] = picojson::value{0.0};
    payload["backend"] = picojson::value{"test-bytes"};
    picojson::object optsObj;
    optsObj["payload"] = picojson::value{payload};
    auto const optsStr = picojson::value{optsObj}.serialize();

    mxlFlowWriter writer;
    mxlFlowConfigInfo configInfo;
    bool flowWasCreated = false;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), optsStr.c_str(), &writer, &configInfo, &flowWasCreated) == MXL_STATUS_OK);
    REQUIRE(flowWasCreated);
    REQUIRE(configInfo.common.payloadLocation == MXL_PAYLOAD_LOCATION_DEVICE_MEMORY);

    auto const rate = mxlRational{60000, 1001};
    auto const index = mxlTimestampToIndex(&rate, mxlGetTime());
    mxlGrainInfo gInfo{};
    mxlPayloadView writeView{};
    REQUIRE(mxlFlowWriterOpenGrainEx(writer, index, &gInfo, &writeView) == MXL_STATUS_OK);
    REQUIRE(writeView.kind == MXL_PAYLOAD_KIND_HOST_PTR);
    REQUIRE(writeView.u.hostPtr != nullptr);
    std::memset(writeView.u.hostPtr, 0x3C, 16);
    gInfo.validSlices = gInfo.totalSlices;
    REQUIRE(mxlFlowWriterCommitGrain(writer, &gInfo) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, "5fbec3b1-1b0f-417d-9059-8b94a47197ed", "", &reader) == MXL_STATUS_OK);
    mxlPayloadView readView{};
    REQUIRE(mxlFlowReaderGetGrainNonBlockingEx(reader, index, &gInfo, &readView) == MXL_STATUS_OK);
    REQUIRE(readView.kind == MXL_PAYLOAD_KIND_HOST_PTR);
    REQUIRE(std::memcmp(readView.u.hostPtr, writeView.u.hostPtr, 16) == 0);

    auto const payloadJson = domain / "5fbec3b1-1b0f-417d-9059-8b94a47197ed.mxl-flow" / "payload.json";
    REQUIRE(fs::exists(payloadJson));

    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}
