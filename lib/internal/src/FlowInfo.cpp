// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowInfo.cpp
 * @brief Stream output operator for mxlFlowInfo structures
 *
 * This file implements pretty-printing of flow information for debugging and logging.
 * The output includes all relevant flow configuration and runtime state in a
 * human-readable format.
 *
 * The operator<< implementation is useful for:
 * - Debugging flow creation and configuration
 * - Logging flow state in error messages
 * - Diagnostic tools that inspect flow metadata
 *
 * Output format example:
 *   - Flow [550e8400-e29b-41d4-a716-446655440000]
 *       Version: 1
 *       Format: Video
 *       Grain/sample rate: 30000/1001
 *       ...
 */

#include "mxl-internal/FlowInfo.hpp"
#include <cstdint>
#include <ostream>
#include <uuid.h>
#include <fmt/format.h>

namespace
{
    /**
     * @brief Convert mxlDataFormat enum to human-readable string
     *
     * @param format The data format enum value
     * @return String representation of the format
     */
    constexpr char const* getFormatString(int format) noexcept
    {
        switch (format)
        {
            case MXL_DATA_FORMAT_UNSPECIFIED: return "UNSPECIFIED";
            case MXL_DATA_FORMAT_VIDEO:       return "Video";
            case MXL_DATA_FORMAT_AUDIO:       return "Audio";
            case MXL_DATA_FORMAT_DATA:        return "Data";
            default:                          return "UNKNOWN";
        }
    }

    /**
     * @brief Convert payload location enum to human-readable string
     *
     * MXL supports both host memory (standard RAM) and device memory (GPU memory)
     * for payloads. Currently only host memory is implemented.
     *
     * @param payloadLocation The payload location enum value
     * @return String representation ("Host" or "Device")
     */
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

/**
 * @brief Output stream operator for pretty-printing flow information
 *
 * This operator formats an mxlFlowInfo structure into a multi-line, human-readable
 * representation. It's useful for debugging, logging, and diagnostic tools.
 *
 * The output includes:
 * - Flow UUID
 * - Version and structure size (for compatibility checking)
 * - Flow configuration (format, rates, batch sizes, payload location)
 * - Format-specific configuration (grain count for discrete, channel count for continuous)
 * - Runtime state (head index, last read/write timestamps)
 *
 * @param os Output stream to write to
 * @param info The flow information structure to format
 * @return Reference to the output stream (for chaining)
 *
 * Example output:
 * @code
 *   - Flow [550e8400-e29b-41d4-a716-446655440000]
 *             Version: 1
 *         Struct size: 2048
 *              Format: Video
 *     Grain/sample rate: 30000/1001
 *   Commit batch size: 1080
 *      Sync batch size: 1080
 *    Payload Location: Host
 *        Device Index: -1
 *               Flags: 00000000
 *         Grain count: 20
 *
 *          Head index: 1523
 *     Last write time: 1234567890123456789
 *      Last read time: 1234567890123456789
 * @endcode
 */
MXL_EXPORT
std::ostream& operator<<(std::ostream& os, mxlFlowInfo const& info)
{
    // Convert the UUID bytes to a printable string
    auto const span = uuids::span<std::uint8_t, sizeof info.config.common.id>{
        const_cast<std::uint8_t*>(info.config.common.id), sizeof info.config.common.id};
    auto const id = uuids::uuid(span);

    // Output common flow configuration
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

    // Output format-specific configuration
    if (mxlIsDiscreteDataFormat(info.config.common.format))
    {
        // Discrete flows (video/data) have a grain count
        os << '\t' << fmt::format("{: >18}: {}", "Grain count", info.config.discrete.grainCount) << '\n';
    }
    else if (mxlIsContinuousDataFormat(info.config.common.format))
    {
        // Continuous flows (audio) have channel count and buffer length
        os << '\t' << fmt::format("{: >18}: {}", "Channel count", info.config.continuous.channelCount) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Buffer length", info.config.continuous.bufferLength) << '\n';
    }

    // Output runtime state
    os << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Head index", info.runtime.headIndex) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Last write time", info.runtime.lastWriteTime) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Last read time", info.runtime.lastReadTime) << '\n';

    return os;
}
