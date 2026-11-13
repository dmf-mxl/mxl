// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstdlib>
#include <catch2/catch_test_macros.hpp>
#include <mxl/dataformat.h>
#include <mxl/flowinfo.h>
#include <mxl/rational.h>
#include "Latency.hpp"

namespace
{
    /**
     * @brief Build flow info that sets only the fields the length helpers read.
     * @param[in] format Data format of the flow.
     * @param[in] length Grain count for discrete flows, buffer length in samples for continuous flows. Not used for
     *            other formats.
     * @return The flow info.
     */
    ::mxlFlowInfo makeFlowInfo(std::uint32_t format, std::uint32_t length) noexcept
    {
        auto info = ::mxlFlowInfo{};
        info.config.common.format = format;
        if (::mxlIsDiscreteDataFormat(static_cast<int>(format)))
        {
            info.config.discrete.grainCount = length;
        }
        else if (::mxlIsContinuousDataFormat(static_cast<int>(format)))
        {
            info.config.continuous.bufferLength = length;
        }
        return info;
    }
}

TEST_CASE("Ring and history length", "[mxl-info][latency]")
{
    using mxl::info::historyLength;
    using mxl::info::ringLength;

    // A 30000/1001 video flow with the default 200 ms history has 5 grains. The writer keeps one, so readers reach 4.
    auto const video = makeFlowInfo(MXL_DATA_FORMAT_VIDEO, 5);
    CHECK(ringLength(video) == 5);
    CHECK(historyLength(video) == 4);

    auto const data = makeFlowInfo(MXL_DATA_FORMAT_DATA, 5);
    CHECK(ringLength(data) == 5);
    CHECK(historyLength(data) == 4);

    // A 48 kHz audio flow with the default 200 ms history has 19456 samples. Only half is readable.
    auto const audio = makeFlowInfo(MXL_DATA_FORMAT_AUDIO, 19456);
    CHECK(ringLength(audio) == 19456);
    CHECK(historyLength(audio) == 9728);

    auto const event = makeFlowInfo(MXL_DATA_FORMAT_EVENT, 0);
    CHECK(ringLength(event) == 0);
    CHECK(historyLength(event) == 0);

    // A one-grain ring has no readable history, but it still has a ring, so it is not taken for an event flow.
    auto const oneGrain = makeFlowInfo(MXL_DATA_FORMAT_VIDEO, 1);
    CHECK(ringLength(oneGrain) == 1);
    CHECK(historyLength(oneGrain) == 0);
}

TEST_CASE("Latency level thresholds", "[mxl-info][latency]")
{
    using mxl::info::LatencyLevel;
    using mxl::info::latencyLevel;

    // Video with 5 grains has a history of 4: normal up to 2, warning for 3 and 4, error from 5.
    CHECK(latencyLevel(0, 4, false) == LatencyLevel::Normal);
    CHECK(latencyLevel(2, 4, false) == LatencyLevel::Normal);
    CHECK(latencyLevel(3, 4, false) == LatencyLevel::Warning);
    CHECK(latencyLevel(4, 4, false) == LatencyLevel::Warning);
    CHECK(latencyLevel(5, 4, false) == LatencyLevel::Error);

    // Audio with a 19456-sample buffer has a history of 9728 samples.
    CHECK(latencyLevel(4864, 9728, false) == LatencyLevel::Normal);
    CHECK(latencyLevel(4865, 9728, false) == LatencyLevel::Warning);
    CHECK(latencyLevel(9728, 9728, false) == LatencyLevel::Warning);
    CHECK(latencyLevel(9729, 9728, false) == LatencyLevel::Error);

    // A head ahead of the current time is an error at any latency.
    CHECK(latencyLevel(0, 4, true) == LatencyLevel::Error);
}

TEST_CASE("Wait timeout", "[mxl-info][latency]")
{
    using mxl::info::waitTimeoutNs;

    // 4 grains at 30000/1001 is 133466666.7 ns.
    auto video = makeFlowInfo(MXL_DATA_FORMAT_VIDEO, 5);
    video.config.common.grainRate = ::mxlRational{30000, 1001};
    CHECK(waitTimeoutNs(video) == 133'466'667U);

    // 9728 samples at 48 kHz is 202666666.7 ns.
    auto audio = makeFlowInfo(MXL_DATA_FORMAT_AUDIO, 19456);
    audio.config.common.grainRate = ::mxlRational{48000, 1};
    CHECK(waitTimeoutNs(audio) == 202'666'667U);

    // A one-grain ring has no history, so the read does not block.
    auto oneGrain = makeFlowInfo(MXL_DATA_FORMAT_VIDEO, 1);
    oneGrain.config.common.grainRate = ::mxlRational{30000, 1001};
    CHECK(waitTimeoutNs(oneGrain) == 0U);

    // A zero grain rate cannot be converted.
    auto const noRate = makeFlowInfo(MXL_DATA_FORMAT_VIDEO, 5);
    CHECK(waitTimeoutNs(noRate) == MXL_UNDEFINED_INDEX);
}
