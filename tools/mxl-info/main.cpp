// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file mxl-info/main.cpp
 * @brief MXL flow inspection and management utility
 *
 * This tool provides command-line utilities for inspecting and managing MXL flows within a domain.
 * It can list all flows, display detailed information about specific flows, and perform garbage
 * collection of inactive flows.
 *
 * Usage examples:
 *   - List all flows:                    mxl-info -d /tmp/mxl-domain --list
 *   - Show specific flow info:           mxl-info -d /tmp/mxl-domain -f <flow-uuid>
 *   - Garbage collect inactive flows:    mxl-info -d /tmp/mxl-domain --garbage-collect
 *   - Use URI format:                    mxl-info mxl:///tmp/mxl-domain?id=<flow-uuid>
 *
 * The tool displays comprehensive flow information including:
 *   - Flow format (video/audio/data)
 *   - Grain/sample rate and batch size hints
 *   - Runtime state (head index, last write/read times)
 *   - Current latency in grains/samples (color-coded when output to terminal)
 *   - Whether the flow is currently active
 */

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <uuid.h>
#include <sys/file.h>
#include <CLI/CLI.hpp>
#include <fmt/color.h>
#include <fmt/format.h>
#include <gsl/span>
#include <picojson/picojson.h>
#include <mxl/flow.h>
#include <mxl/flowinfo.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "uri_parser.h"

namespace
{
    namespace detail
    {
        /**
         * @brief Helper struct for formatting flow info with latency calculation
         *
         * This wrapper enables custom formatting of mxlFlowInfo that includes
         * real-time latency calculation based on the current timestamp.
         */
        struct LatencyPrinter
        {
            constexpr explicit LatencyPrinter(::mxlFlowInfo const& info) noexcept
                : flowInfo{&info}
            {}

            ::mxlFlowInfo const* flowInfo;
        };

        /**
         * @brief Detect if an output stream is connected to a terminal
         *
         * Used to determine whether to apply ANSI color codes for latency display.
         * @param os The output stream to check
         * @return true if the stream is connected to a terminal, false otherwise
         */
        bool isTerminal(std::ostream& os) noexcept
        {
            if (&os == &std::cout)
            {
                return ::isatty(::fileno(stdout)) != 0;
            }
            if ((&os == &std::cerr) || (&os == &std::clog))
            {
                return ::isatty(::fileno(stderr)) != 0;
            }
            return false; // treat all other ostreams as non-terminal
        }

        /**
         * @brief Output flow latency with color-coding based on buffer usage
         *
         * Calculates the current latency by comparing the head index (last committed grain/sample)
         * with the current time index. Color codes the output:
         *   - Green: latency is below the buffer limit (healthy)
         *   - Yellow: latency exactly equals the buffer limit (at capacity)
         *   - Red: latency exceeds the buffer limit (buffer overrun risk)
         *
         * @param os Output stream
         * @param headIndex Last committed grain/sample index in the ring buffer
         * @param grainRate Flow's grain/sample rate for index-to-time conversion
         * @param limit Buffer capacity (grainCount for video, bufferLength for audio)
         */
        void outputLatency(std::ostream& os, std::uint64_t headIndex, ::mxlRational const& grainRate, std::uint64_t limit)
        {
            auto const now = ::mxlGetTime();

            auto const currentIndex = ::mxlTimestampToIndex(&grainRate, now);
            auto const latency = currentIndex - headIndex;

            if (isTerminal(os))
            {
                auto color = fmt::color::green;
                if (latency > limit)
                {
                    color = fmt::color::red;
                }
                else if (latency == limit)
                {
                    color = fmt::color::yellow;
                }

                os << '\t' << fmt::format(fmt::fg(color), "{: >18}: {}", "Latency (grains)", latency) << std::endl;
            }
            else
            {
                os << '\t' << fmt::format("{: >18}: {}", "Latency (grains)", latency) << std::endl;
            }
        }

        /**
         * @brief Convert MXL data format enum to human-readable string
         * @param format The MXL_DATA_FORMAT_* value
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
         * @brief Convert MXL payload location enum to human-readable string
         * @param payloadLocation The MXL_PAYLOAD_LOCATION_* value
         * @return String representation of the payload location (Host/Device memory)
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

        /**
         * @brief Stream operator for formatted output of mxlFlowInfo
         *
         * Outputs comprehensive flow information including configuration and runtime state.
         * For discrete flows (video/data), displays grain count and per-grain parameters.
         * For continuous flows (audio), displays channel count and buffer length.
         *
         * @param os Output stream
         * @param info Flow information structure
         * @return Reference to the output stream for chaining
         */
        std::ostream& operator<<(std::ostream& os, mxlFlowInfo const& info)
        {
            auto const span = uuids::span<std::uint8_t, sizeof info.config.common.id>{
                const_cast<std::uint8_t*>(info.config.common.id), sizeof info.config.common.id};
            auto const id = uuids::uuid(span);
            os << "- Flow [" << uuids::to_string(id) << ']' << '\n'
               << '\t' << fmt::format("{: >18}: {}", "Version", info.version) << '\n'
               << '\t' << fmt::format("{: >18}: {}", "Struct size", info.size) << '\n'
               << '\t' << fmt::format("{: >18}: {}", "Format", getFormatString(info.config.common.format)) << '\n'
               << '\t'
               << fmt::format("{: >18}: {}/{}", "Grain/sample rate", info.config.common.grainRate.numerator, info.config.common.grainRate.denominator)
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

        /**
         * @brief Stream operator for LatencyPrinter that includes latency calculation
         *
         * Outputs all flow information plus calculates and displays the current latency.
         * Uses the appropriate limit (grainCount or bufferLength) based on flow type.
         *
         * @param os Output stream
         * @param lp LatencyPrinter containing flow information
         * @return Reference to the output stream for chaining
         */
        std::ostream& operator<<(std::ostream& os, LatencyPrinter const& lp)
        {
            os << *lp.flowInfo;

            if (::mxlIsDiscreteDataFormat(lp.flowInfo->config.common.format))
            {
                outputLatency(os, lp.flowInfo->runtime.headIndex, lp.flowInfo->config.common.grainRate, lp.flowInfo->config.discrete.grainCount);
            }
            else if (::mxlIsContinuousDataFormat(lp.flowInfo->config.common.format))
            {
                outputLatency(os, lp.flowInfo->runtime.headIndex, lp.flowInfo->config.common.grainRate, lp.flowInfo->config.continuous.bufferLength);
            }
            return os;
        }
    }

    /**
     * @brief Factory function to create a LatencyPrinter for a flow
     * @param info Flow information to wrap
     * @return LatencyPrinter that can be streamed to display info with latency
     */
    detail::LatencyPrinter formatWithLatency(::mxlFlowInfo const& info)
    {
        return detail::LatencyPrinter{info};
    }

    /**
     * @brief RAII wrapper for MXL instance lifecycle management
     *
     * Ensures proper cleanup of MXL instance resources using RAII pattern.
     * The instance is automatically destroyed when the object goes out of scope.
     */
    class ScopedMxlInstance
    {
    public:
        /**
         * @brief Construct and initialize an MXL instance
         * @param domain Path to the MXL domain directory (tmpfs)
         * @throws std::runtime_error if instance creation fails
         */
        explicit ScopedMxlInstance(std::string const& domain)
            : _instance{::mxlCreateInstance(domain.c_str(), "")}
        {
            if (_instance == nullptr)
            {
                throw std::runtime_error{"Failed to create MXL instance."};
            }
        }

        ScopedMxlInstance(ScopedMxlInstance&&) = delete;
        ScopedMxlInstance(ScopedMxlInstance const&) = delete;

        ScopedMxlInstance& operator=(ScopedMxlInstance&&) = delete;
        ScopedMxlInstance& operator=(ScopedMxlInstance const&) = delete;

        /**
         * @brief Destructor ensures MXL instance is properly destroyed
         */
        ~ScopedMxlInstance()
        {
            // Guaranteed to be non-null if the destructor runs
            ::mxlDestroyInstance(_instance);
        }

        /** @brief Get the raw MXL instance handle */
        constexpr ::mxlInstance get() const noexcept
        {
            return _instance;
        }

        /** @brief Implicit conversion operator for use in MXL API calls */
        constexpr operator ::mxlInstance() const noexcept
        {
            return _instance;
        }

    private:
        ::mxlInstance _instance;
    };

    /**
     * @brief Extract human-readable metadata from NMOS flow definition JSON
     *
     * Parses the flow definition JSON to extract the label and group hint tag.
     * These provide user-friendly names for flows when listing them.
     *
     * @param flowDef JSON string containing NMOS flow definition
     * @return Pair of (label, groupHint) strings, or ("n/a", "n/a") if parsing fails
     */
    std::pair<std::string, std::string> getFlowDetails(std::string const& flowDef) noexcept
    {
        auto result = std::pair<std::string, std::string>{"n/a", "n/a"};

        try
        {
            auto v = picojson::value{};
            if (auto const errorString = picojson::parse(v, flowDef); errorString.empty())
            {
                auto const& obj = v.get<picojson::object>();

                // Extract the "label" field (user-friendly flow name)
                if (auto const labelIt = obj.find("label"); (labelIt != obj.end()) && labelIt->second.is<std::string>())
                {
                    result.first = labelIt->second.get<std::string>();
                }

                // Extract the NMOS "grouphint" tag (for flow grouping/categorization)
                if (auto const tagsIt = obj.find("tags"); (tagsIt != obj.end()) && tagsIt->second.is<picojson::object>())
                {
                    auto const& tagsObj = tagsIt->second.get<picojson::object>();

                    if (auto const groupHintIt = tagsObj.find("urn:x-nmos:tag:grouphint/v1.0");
                        (groupHintIt != tagsObj.end()) && groupHintIt->second.is<picojson::array>())
                    {
                        auto const& groupHintArray = groupHintIt->second.get<picojson::array>();
                        if (!groupHintArray.empty() && groupHintArray[0].is<std::string>())
                        {
                            result.second = groupHintArray[0].get<std::string>();
                        }
                    }
                }
            }
        }
        catch (...)
        {}

        return result;
    }

    /**
     * @brief List all flows in the specified MXL domain
     *
     * Scans the domain directory for .mxl-flow subdirectories, validates their UUIDs,
     * and outputs a CSV-formatted list of flows with their IDs, labels, and group hints.
     *
     * @param in_domain Path to the MXL domain directory
     * @return EXIT_SUCCESS on success, EXIT_FAILURE on error
     */
    int listAllFlows(std::string const& in_domain)
    {
        auto const opts = "{}";
        auto instance = mxlCreateInstance(in_domain.c_str(), opts);
        if (instance == nullptr)
        {
            std::cerr << "ERROR" << ": "
                      << "Failed to create MXL instance." << std::endl;
            return EXIT_FAILURE;
        }

        if (auto const base = std::filesystem::path{in_domain}; is_directory(base))
        {
            // Iterate through domain directory looking for flow subdirectories
            for (auto const& entry : std::filesystem::directory_iterator{base})
            {
                // MXL flows are stored in directories with .mxl-flow extension
                if (is_directory(entry) && (entry.path().extension().string() == ".mxl-flow"))
                {
                    // The directory stem should be a valid UUID (the flow ID)
                    auto const id = uuids::uuid::from_string(entry.path().stem().string());
                    if (id.has_value())
                    {
                        char fourKBuffer[4096];
                        auto fourKBufferSize = sizeof(fourKBuffer);
                        auto requiredBufferSize = fourKBufferSize;

                        // Retrieve the NMOS flow definition JSON
                        if (mxlGetFlowDef(instance, uuids::to_string(*id).c_str(), fourKBuffer, &requiredBufferSize) != MXL_STATUS_OK)
                        {
                            std::cerr << "ERROR" << ": "
                                      << "Failed to get flow definition for flow id " << uuids::to_string(*id) << std::endl;
                            continue;
                        }

                        auto flowDef = std::string{fourKBuffer, requiredBufferSize - 1};
                        auto const [label, groupHint] = getFlowDetails(flowDef);

                        // Output CSV format: id, "label", "group_hint"
                        std::cout << *id << ", " << std::quoted(label) << ", " << std::quoted(groupHint) << '\n';
                    }
                }
            }
        }

        if ((instance != nullptr) && (mxlDestroyInstance(instance)) != MXL_STATUS_OK)
        {
            std::cerr << "ERROR" << ": "
                      << "Failed to destroy MXL instance." << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << std::endl;
        return EXIT_SUCCESS;
    }

    /**
     * @brief Display detailed information about a specific flow
     *
     * Creates a flow reader to query comprehensive flow information including:
     *   - Configuration (format, rate, batch sizes, etc.)
     *   - Runtime state (head index, timestamps)
     *   - Current latency
     *   - Active status
     *
     * @param in_domain Path to the MXL domain directory
     * @param in_id UUID string of the flow to inspect
     * @return EXIT_SUCCESS on success, EXIT_FAILURE on error
     */
    int printFlow(std::string const& in_domain, std::string const& in_id)
    {
        // Create the SDK instance with a specific domain
        auto const instance = ScopedMxlInstance{in_domain};

        // Create a flow reader for the given flow id
        auto reader = ::mxlFlowReader{};
        if (::mxlCreateFlowReader(instance, in_id.c_str(), "", &reader) == MXL_STATUS_OK)
        {
            // Extract the mxlFlowInfo structure (combines config and runtime info)
            auto info = ::mxlFlowInfo{};
            auto const getInfoStatus = ::mxlFlowReaderGetInfo(reader, &info);
            ::mxlReleaseFlowReader(instance, reader);

            if (getInfoStatus == MXL_STATUS_OK)
            {
                // Print comprehensive flow info with latency calculation
                std::cout << formatWithLatency(info);

                // Check and display whether the flow is currently active
                auto active = false;
                if (auto const status = ::mxlIsFlowActive(instance, in_id.c_str(), &active); status == MXL_STATUS_OK)
                {
                    std::cout << '\t' << fmt::format("{: >18}: {}", "Active", active) << std::endl;
                }
                else
                {
                    std::cerr << "ERROR" << ": "
                              << "Failed to check if flow is active: " << status << std::endl;
                }

                return EXIT_SUCCESS;
            }
            else
            {
                std::cerr << "ERROR: " << "Failed to get flow info" << std::endl;
            }
        }
        else
        {
            std::cerr << "ERROR" << ": "
                      << "Failed to create flow reader." << std::endl;
        }

        return EXIT_FAILURE;
    }

    /**
     * @brief Perform garbage collection on inactive flows in the domain
     *
     * Removes flows that are no longer active (neither reader nor writer attached).
     * This helps clean up orphaned flow resources from the shared memory domain.
     *
     * @param in_domain Path to the MXL domain directory
     * @return EXIT_SUCCESS on success, EXIT_FAILURE on error
     */
    int garbageCollect(std::string const& in_domain)
    {
        // Create the SDK instance with a specific domain
        auto const instance = ScopedMxlInstance{in_domain};

        if (auto const status = ::mxlGarbageCollectFlows(instance); status == MXL_STATUS_OK)
        {
            return EXIT_SUCCESS;
        }
        else
        {
            std::cerr << "ERROR" << ": "
                      << "Failed to perform garbage collection" << ": " << status << std::endl;
            return EXIT_FAILURE;
        }
    }
}

/**
 * @brief Main entry point for mxl-info tool
 *
 * Parses command-line arguments and executes the requested operation:
 *   1. Garbage collection (if -g flag is set)
 *   2. List all flows (if -l flag is set or no flow ID specified)
 *   3. Display specific flow info (if flow ID is provided)
 *
 * Supports both traditional command-line options and MXL URI format.
 *
 * @param argc Argument count
 * @param argv Argument values
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
int main(int argc, char** argv)
{
    auto app = CLI::App{"mxl-info"};
    app.allow_extras();
    app.footer("MXL URI format:\n"
               "    mxl://[authority[:port]]/domain[?id=...]\n"
               "    See: https://github.com/dmf-mxl/mxl/docs/Addressability.md");

    auto version = ::mxlVersionType{};
    ::mxlGetVersion(&version);
    app.set_version_flag("--version", version.full);

    auto domain = std::string{};
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->check(CLI::ExistingDirectory);

    auto flowId = std::string{};
    app.add_option("-f,--flow", flowId, "The flow id to analyse");

    auto listOpt = app.add_flag("-l,--list", "List all flows in the MXL domain");
    auto gcOpt = app.add_flag("-g,--garbage-collect", "Garbage collect inactive flows found in the MXL domain");

    auto address = std::vector<std::string>{};
    app.add_option("ADDRESS", address, "MXL URI")->expected(-1);

    CLI11_PARSE(app, argc, argv);

    // URI will overwrite any other redundant options. Parse the URI after CLI11 parsing.
    // This allows flexible syntax: "mxl-info -d /tmp/domain -f uuid" or "mxl-info mxl:///tmp/domain?id=uuid"
    if (!address.empty())
    {
        auto parsedUri = uri::parse_uri(address.at(0));

        if (parsedUri.path.empty())
        {
            std::cerr << "ERROR: Domain must be specified in the MXL URI." << std::endl;
            return EXIT_FAILURE;
        }

        if (!parsedUri.authority.host.empty() || (parsedUri.authority.port != 0))
        {
            std::cerr << "ERROR: Authority/port not currently supported in MXL URI." << std::endl;
            return EXIT_FAILURE;
        }

        // Extract domain path from URI
        domain = parsedUri.path;

        // Extract flow ID from query parameter if present
        if (parsedUri.query.find("id") != parsedUri.query.end())
        {
            flowId = parsedUri.query.at("id");
        }
    }

    // At this point we must have a domain.
    if (domain.empty())
    {
        std::cerr << "ERROR: Domain must be specified either via --domain or in the URI." << std::endl;
        return EXIT_FAILURE;
    }

    auto status = EXIT_SUCCESS;

    // Execute the requested operation based on command-line flags and arguments

    // Priority 1: Garbage collection (if -g flag is set)
    if (gcOpt->count() > 0)
    {
        status = garbageCollect(domain);
    }
    // Priority 2: List all flows (if -l flag is set or no flow ID specified)
    else if (listOpt->count() > 0 || flowId.empty())
    {
        status = listAllFlows(domain);
    }
    // Priority 3: Display specific flow info (if flow ID is provided via -f or URI)
    else if (!flowId.empty())
    {
        status = printFlow(domain, flowId);
    }
    else
    {
        std::cerr << "No action specified. Use --help for usage information." << std::endl;
        status = EXIT_FAILURE;
    }

    return status;
}
