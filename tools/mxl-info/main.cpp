// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

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
#include <picojson/wrapper.h>
#include <mxl/flow.h>
#include <mxl/flowinfo.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "uri_parser.h"

namespace
{
    namespace detail
    {
        struct LatencyPrinter
        {
            constexpr explicit LatencyPrinter(::mxlFlowInfo const& info) noexcept
                : flowInfo{&info}
            {}

            ::mxlFlowInfo const* flowInfo;
        };

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

        constexpr char const* getPayloadLocationString(std::uint32_t payloadLocation) noexcept
        {
            switch (payloadLocation)
            {
                case MXL_PAYLOAD_LOCATION_HOST_MEMORY:   return "Host";
                case MXL_PAYLOAD_LOCATION_DEVICE_MEMORY: return "Device";
                default:                                 return "UNKNOWN";
            }
        }

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

    detail::LatencyPrinter formatWithLatency(::mxlFlowInfo const& info)
    {
        return detail::LatencyPrinter{info};
    }

    class ScopedMxlInstance
    {
    public:
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

        ~ScopedMxlInstance()
        {
            // Guarateed to be non-null if the destructor runs
            ::mxlDestroyInstance(_instance);
        }

        constexpr ::mxlInstance get() const noexcept
        {
            return _instance;
        }

        constexpr operator ::mxlInstance() const noexcept
        {
            return _instance;
        }

    private:
        ::mxlInstance _instance;
    };

    std::pair<std::string, std::string> getFlowDetails(std::string const& flowDef) noexcept
    {
        auto result = std::pair<std::string, std::string>{"n/a", "n/a"};

        try
        {
            auto v = picojson::value{};
            if (auto const errorString = picojson::parse(v, flowDef); errorString.empty())
            {
                auto const& obj = v.get<picojson::object>();

                if (auto const labelIt = obj.find("label"); (labelIt != obj.end()) && labelIt->second.is<std::string>())
                {
                    result.first = labelIt->second.get<std::string>();
                }

                // try to get the group hint tag
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
            for (auto const& entry : std::filesystem::directory_iterator{base})
            {
                if (is_directory(entry) && (entry.path().extension().string() == ".mxl-flow"))
                {
                    // this looks like a uuid. try to parse it an confirm it is valid.
                    auto const id = uuids::uuid::from_string(entry.path().stem().string());
                    if (id.has_value())
                    {
                        char fourKBuffer[4096];
                        auto fourKBufferSize = sizeof(fourKBuffer);
                        auto requiredBufferSize = fourKBufferSize;

                        if (mxlGetFlowDef(instance, uuids::to_string(*id).c_str(), fourKBuffer, &requiredBufferSize) != MXL_STATUS_OK)
                        {
                            std::cerr << "ERROR" << ": "
                                      << "Failed to get flow definition for flow id " << uuids::to_string(*id) << std::endl;
                            continue;
                        }

                        auto flowDef = std::string{fourKBuffer, requiredBufferSize - 1};
                        auto const [label, groupHint] = getFlowDetails(flowDef);

                        // Output CSV format: id,label,group_hint
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

    int printFlow(std::string const& in_domain, std::string const& in_id)
    {
        // Create the SDK instance with a specific domain.
        auto const instance = ScopedMxlInstance{in_domain};

        // Create a flow reader for the given flow id.
        auto reader = ::mxlFlowReader{};
        if (::mxlCreateFlowReader(instance, in_id.c_str(), "", &reader) == MXL_STATUS_OK)
        {
            // Extract the mxlFlowInfo structure.
            auto info = ::mxlFlowInfo{};
            auto const getInfoStatus = ::mxlFlowReaderGetInfo(reader, &info);
            ::mxlReleaseFlowReader(instance, reader);

            if (getInfoStatus == MXL_STATUS_OK)
            {
                std::cout << formatWithLatency(info);

                auto active = false;
                if (auto const status = ::mxlIsFlowActive(instance, in_id.c_str(), &active); status == MXL_STATUS_OK)
                {
                    // Print the status of the flow.
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

    // Perform garbage collection on the MXL domain.
    int garbageCollect(std::string const& in_domain)
    {
        // Create the SDK instance with a specific domain.
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

    // URI will overwrite any other redudant options.  Parse the URI after CLI11 parsing.
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

        // We know that there won't be any error since the URI passed CLI11 validation.
        domain = parsedUri.path;

        // Check for the first flow id in the query parameters.
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

    // If garbage collect is specified, do that first.
    if (gcOpt->count() > 0)
    {
        status = garbageCollect(domain);
    }
    // If list all is specified or if we don't have a flow id specified through option or URI: list all flows.
    else if (listOpt->count() > 0 || flowId.empty())
    {
        status = listAllFlows(domain);
    }
    // The user specified a flow id (through the URI or command line option): print info for that flow.
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
