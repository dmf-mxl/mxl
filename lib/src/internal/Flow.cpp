#include "Flow.hpp"
#include <cstdint>
#include <ctime>
#include <ostream>
#include <fmt/format.h>
#include <uuid.h>
#include <mxl/flow.h>

namespace mxl::lib
{
    namespace
    {
        constexpr char const* getFormatString(int format) noexcept
        {
            switch (format)
            {
                case MXL_DATA_FORMAT_UNSPECIFIED:   return "UNSPECIFIED";
                case MXL_DATA_FORMAT_VIDEO:         return "Video";
                case MXL_DATA_FORMAT_AUDIO:         return "Audio";
                case MXL_DATA_FORMAT_DATA:          return "Data";
                case MXL_DATA_FORMAT_MUX:           return "Multiplexed";
                default:                            return "UNKNOWN";
            }
        }
    }

    std::ostream& operator<<(std::ostream& os, Flow const& flow)
    {
        auto const span = uuids::span<std::uint8_t, sizeof flow.info.common.id>{const_cast<std::uint8_t*>(flow.info.common.id), sizeof flow.info.common.id};
        auto const id = uuids::uuid(span);
        os << "- Flow [" << uuids::to_string(id) << ']' << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Version", flow.info.version) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Struct size", flow.info.size) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Last write time", flow.info.common.lastWriteTime) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Last read time", flow.info.common.lastReadTime) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Format", getFormatString(flow.info.common.format)) << '\n'
           << '\t' << fmt::format("{: >18}: {:0>8x}", "Flags", flow.info.common.flags) << '\n';

        if (mxlIsDiscreteDataFormat(flow.info.common.format))
        {
           os << '\t' << fmt::format("{: >18}: {}/{}", "Grain rate", flow.info.discrete.grainRate.numerator, flow.info.discrete.grainRate.denominator) << '\n'
              << '\t' << fmt::format("{: >18}: {}", "Grain count", flow.info.discrete.grainCount) << '\n'
              << '\t' << fmt::format("{: >18}: {}", "Head index", flow.info.discrete.headIndex) << '\n';
        }
        else if (mxlIsContinuousDataFormat(flow.info.common.format))
        {
           os << '\t' << fmt::format("{: >18}: {}/{}", "Sample rate", flow.info.continuous.sampleRate.numerator, flow.info.continuous.sampleRate.denominator) << '\n'
              << '\t' << fmt::format("{: >18}: {}", "Channel count", flow.info.continuous.channelCount) << '\n'
              << '\t' << fmt::format("{: >18}: {}", "Buffer length", flow.info.continuous.bufferLength) << '\n'
              << '\t' << fmt::format("{: >18}: {}", "Commit batch size", flow.info.continuous.commitBatchSize) << '\n'
              << '\t' << fmt::format("{: >18}: {}", "Sync batch size", flow.info.continuous.syncBatchSize) << '\n'
              << '\t' << fmt::format("{: >18}: {}", "Head index", flow.info.continuous.headIndex) << '\n';
        }

        return os;
    }

    std::ostream& operator<<(std::ostream& os, Grain const& grain)
    {
        auto const location = (grain.header.info.payloadLocation == PAYLOAD_LOCATION_HOST_MEMORY) ? "host" : "device";
        os << "- Grain" << '\n'
           << '\t' << fmt::format("{: >14}: {}", "Version", grain.header.info.version) << '\n'
           << '\t' << fmt::format("{: >14}: {}", "Struct size", grain.header.info.size) << '\n'
           << '\t' << fmt::format("{: >14}: {:0>8x}", "Flags", grain.header.info.flags) << '\n'
           << '\t' << fmt::format("{: >14}: {}", "Location", location) << '\n'
           << '\t' << fmt::format("{: >14}: {}", "Device", grain.header.info.deviceIndex) << '\n'
           << '\t' << fmt::format("{: >14}: {}", "Payload size", grain.header.info.grainSize) << '\n';

        return os;
    }
}
