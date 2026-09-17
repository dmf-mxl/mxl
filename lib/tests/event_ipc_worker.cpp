// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Child-process consumer for event IPC regression tests.
 */

#include <string>
#include <unistd.h>
#include <mxl/flow.h>

/**
 * @brief Verify event delivery in a separate reader process.
 * @param argc Must be seven, including the executable name.
 * @param argv Domain, flow ID, first expected timestamp, entry count,
 * readiness pipe descriptor and start pipe descriptor follow the executable name.
 * @return Zero if delivery, payload validation and teardown succeed; nonzero otherwise.
 */
int main(int argc, char** argv)
{
    if (argc != 7)
    {
        return 1;
    }
    auto const offset = std::stoull(argv[3]);
    auto const count = std::stoull(argv[4]);
    auto instance = mxlCreateInstance(argv[1], "{}");
    auto reader = mxlFlowReader{};
    if (!instance || mxlCreateFlowReader(instance, argv[2], "", &reader) != MXL_STATUS_OK)
    {
        return 2;
    }
    auto ready = char{};
    ready = 'R';
    if (::write(std::stoi(argv[5]), &ready, 1) != 1 || ::read(std::stoi(argv[6]), &ready, 1) != 1)
    {
        return 3;
    }
    for (auto i = std::uint64_t{0}; i < count; ++i)
    {
        auto info = mxlEventInfo{};
        auto payload = static_cast<std::uint8_t*>(nullptr);
        if (mxlFlowReaderGetEvent(reader, 5000000000ULL, &info, &payload) != MXL_STATUS_OK || info.timestamp != offset + i || info.offset != 0 ||
            info.complete != 1 || info.eventSize != info.timestamp % (MXL_DATA_FORMAT_GRAIN_SIZE + 1))
        {
            return 4;
        }
        for (auto byte = std::uint32_t{0}; byte < info.eventSize; ++byte)
        {
            if (payload[byte] != static_cast<std::uint8_t>((info.timestamp * 29) + (std::uint64_t{byte} * 7)))
            {
                return 5;
            }
        }
    }
    if (mxlReleaseFlowReader(instance, reader) != MXL_STATUS_OK || mxlDestroyInstance(instance) != MXL_STATUS_OK)
    {
        return 6;
    }
    return 0;
}
