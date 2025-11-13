// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowInfo.hpp"
#include <cstdint>
#include <ostream>
#include <uuid.h>
#include <fmt/format.h>

namespace
{
    constexpr char const* getFormatString(int format) noexcept
    {
        switch (format)
        {
            case MXL_DATA_FORMAT_UNSPECIFIED: return "UNSPECIFIED";
            case MXL_DATA_FORMAT_VIDEO:       return "Video";
            case MXL_DATA_FORMAT_AUDIO:       return "Audio";
            case MXL_DATA_FORMAT_DATA:        return "Data";
            case MXL_DATA_FORMAT_MUX:         return "Multiplexed";
            default:                          return "UNKNOWN";
        }
    }

    constexpr char const* getPayloadLocationString(std::uint32_t payloadLocation) noexcept
    {
        switch (payloadLocation)
        {
            case MXL_PAYLOAD_LOCATION_HOST_MEMORY:   return "Host";
            case MXL_PAYLOAD_LOCATION_DEVICE_MEMORY: return "Device";
            default:                                 return "UNKNOWN";
        }
    }

}

MXL_EXPORT
std::ostream& operator<<(std::ostream& os, mxlFlowInfo const& info)
{
    auto const span = uuids::span<std::uint8_t, sizeof info.config.common.id>{
        const_cast<std::uint8_t*>(info.config.common.id), sizeof info.config.common.id};
    auto const id = uuids::uuid(span);
    os << "- Flow [" << uuids::to_string(id) << ']' << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Version", info.version) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Struct size", info.size) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Format", getFormatString(info.config.common.format)) << '\n'
       << '\t' << fmt::format("{: >18}: {}/{}", "Grain/sample rate", info.config.common.grainRate.numerator, info.config.common.grainRate.denominator)
       << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Commit batch size", info.config.common.maxCommitBatchSizeHint) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Sync batch size", info.config.common.maxSyncBatchSizeHint) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Payload Location", getPayloadLocationString(info.config.common.payloadLocation)) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Device Index", info.config.common.deviceIndex) << '\n'
       << '\t' << fmt::format("{: >18}: {:0>8x}", "Flags", info.config.common.flags) << '\n';

    if (mxlIsDiscreteDataFormat(info.config.common.format))
    {
        os << '\t' << fmt::format("{: >18}: {}", "Grain count", info.config.discrete.grainCount) << '\n';
    }
    else if (mxlIsContinuousDataFormat(info.config.common.format))
    {
        os << '\t' << fmt::format("{: >18}: {}", "Channel count", info.config.continuous.channelCount) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Buffer length", info.config.continuous.bufferLength) << '\n';
    }

    os << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Head index", info.runtime.headIndex) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Last write time", info.runtime.lastWriteTime) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Last read time", info.runtime.lastReadTime) << '\n';

    return os;
}
