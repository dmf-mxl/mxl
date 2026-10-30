// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Child-process consumer for event IPC regression tests.
 */

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>
#ifdef _WIN32
#   include <windows.h>
#else
#   include <unistd.h>
#endif
#include <mxl/flow.h>
#include <mxl/time.h>

namespace
{
    /**
     * @brief Write one byte to the parent.
     * @param channel Inherited pipe descriptor (POSIX) or handle value (Windows), in decimal.
     */
    bool writeByte(char const* channel, char value)
    {
#ifdef _WIN32
        auto written = DWORD{0};
        return (::WriteFile(reinterpret_cast<HANDLE>(std::stoull(channel)), &value, 1, &written, nullptr) != FALSE) && (written == 1);
#else
        return ::write(std::stoi(channel), &value, 1) == 1;
#endif
    }

    /**
     * @brief Read one byte from the parent.
     * @param channel Inherited pipe descriptor (POSIX) or handle value (Windows), in decimal.
     */
    bool readByte(char const* channel, char& value)
    {
#ifdef _WIN32
        auto read = DWORD{0};
        return (::ReadFile(reinterpret_cast<HANDLE>(std::stoull(channel)), &value, 1, &read, nullptr) != FALSE) && (read == 1);
#else
        return ::read(std::stoi(channel), &value, 1) == 1;
#endif
    }
}

/**
 * @brief Verify event delivery in a separate reader process.
 * @param argc Seven or eight, including the executable name.
 * @param argv Domain, flow ID, first expected timestamp, entry count, readiness pipe and start pipe follow the executable name. An optional
 * last argument is the largest median wake latency in nanoseconds: the time from the commit of an event, taken from the last write time of
 * the flow, to its delivery. The parent must then commit one event at a time, and wait between commits.
 * @return Zero if delivery, payload validation and teardown succeed; nonzero otherwise.
 */
int main(int argc, char** argv)
{
    if ((argc != 7) && (argc != 8))
    {
        return 1;
    }
    auto const offset = std::stoull(argv[3]);
    auto const count = std::stoull(argv[4]);
    auto const maxMedianLatency = (argc == 8) ? std::stoull(argv[7]) : std::uint64_t{0};
    auto instance = mxlCreateInstance(argv[1], "{}");
    auto reader = mxlFlowReader{};
    if ((instance == nullptr) || (mxlCreateFlowReader(instance, argv[2], "", &reader) != MXL_STATUS_OK))
    {
        return 2;
    }
    auto ready = 'R';
    if (!writeByte(argv[5], ready) || !readByte(argv[6], ready))
    {
        return 3;
    }
    auto latencies = std::vector<std::uint64_t>{};
    for (auto i = std::uint64_t{0}; i < count; ++i)
    {
        auto info = mxlEventInfo{};
        auto payload = static_cast<std::uint8_t*>(nullptr);
        if ((mxlFlowReaderGetEvent(reader, 5000000000ULL, &info, &payload) != MXL_STATUS_OK) || (info.timestamp != offset + i) ||
            (info.offset != 0) || (info.complete != 1) || (info.eventSize != info.timestamp % (MXL_DATA_FORMAT_GRAIN_SIZE + 1)))
        {
            return 4;
        }
        if (maxMedianLatency != 0)
        {
            auto const now = mxlGetTime();
            auto runtime = mxlFlowRuntimeInfo{};
            if (mxlFlowReaderGetRuntimeInfo(reader, &runtime) != MXL_STATUS_OK)
            {
                return 7;
            }
            latencies.push_back(now - runtime.lastWriteTime);
        }
        for (auto byte = std::uint32_t{0}; byte < info.eventSize; ++byte)
        {
            if (payload[byte] != static_cast<std::uint8_t>((info.timestamp * 29) + (std::uint64_t{byte} * 7)))
            {
                return 5;
            }
        }
    }
    if (!latencies.empty())
    {
        std::ranges::sort(latencies);
        auto const median = latencies[latencies.size() / 2];
        std::fprintf(stderr,
            "event wake latency: median %llu us, max %llu us\n",
            static_cast<unsigned long long>(median / 1000U),
            static_cast<unsigned long long>(latencies.back() / 1000U));
        if (median > maxMedianLatency)
        {
            return 8;
        }
    }
    if ((mxlReleaseFlowReader(instance, reader) != MXL_STATUS_OK) || (mxlDestroyInstance(instance) != MXL_STATUS_OK))
    {
        return 6;
    }
    return 0;
}
