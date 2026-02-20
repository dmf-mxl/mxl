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

    /// Generate an MXL URI for a given domain and list of flow ids.  This is useful for users to copy and paste into the command line to quickly
    /// access specific flows.
    /// \param domain The MXL domain
    /// \param flowIds The list of flow ids to include in the URI query parameters.
    /// \return An MXL URI string that can be used to access the specified flows in the specified domain.
    std::string generateMxlAddress(std::string const& domain, std::vector<uuids::uuid> const& flowIds)
    {
        std::ostringstream oss;
        oss << "mxl://" << domain << "?";

        for (size_t i = 0; i < flowIds.size(); ++i)
        {
            oss << "id=" << uuids::to_string(flowIds[i]);
            if (i < (flowIds.size() - 1))
            {
                oss << "&";
            }
        }
        return oss.str();
    }

    std::tuple<std::string, std::string, std::string> getFlowDetails(std::string const& flowDef) noexcept
    {
        auto label = std::string{"n/a"};
        auto groupName = std::string{""};
        auto roleInGroup = std::string{""};

        try
        {
            auto v = picojson::value{};
            if (auto const errorString = picojson::parse(v, flowDef); errorString.empty())
            {
                auto const& obj = v.get<picojson::object>();

                if (auto const labelIt = obj.find("label"); (labelIt != obj.end()) && labelIt->second.is<std::string>())
                {
                    label = labelIt->second.get<std::string>();
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
                            // split the string on the first colon to separate group name and role
                            auto const groupHintStr = groupHintArray[0].get<std::string>();
                            auto const colonPos = groupHintStr.find(':');
                            if (colonPos != std::string::npos)
                            {
                                groupName = groupHintStr.substr(0, colonPos);
                                roleInGroup = groupHintStr.substr(colonPos + 1);
                            }
                            else
                            {
                                groupName = groupHintStr;
                            }
                        }
                    }
                }
            }
        }
        catch (...)
        {}

        return {label, groupName, roleInGroup};
    }

    /// Print a list of all flows in the specified MXL domain, grouped by their group name (if specified in the flow definition tags).
    // For each flow, also print the flow label and role in group (if specified).
    // If there are multiple flows with the same role in group within a group, flag this as a conflict in the output.
    // \param in_domain The MXL domain to list flows from.
    // \return EXIT_SUCCESS if the operation was successful, EXIT_FAILURE otherwise.
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

        // maps the group name to a tuple of (flow id, flow label, role in group)
        std::map<std::string, std::vector<std::tuple<uuids::uuid, std::string, std::string>>> groups;

        // List all flows in the domain by iterating over the flow directories.
        if (auto const base = std::filesystem::path{in_domain}; is_directory(base))
        {
            for (auto const& entry : std::filesystem::directory_iterator{base})
            {
                if (is_directory(entry) && (entry.path().extension().string() == ".mxl-flow"))
                {
                    // this looks like a uuid. try to parse it an confirm it is valid.
                    auto const idStr = entry.path().stem().string();
                    auto const id = uuids::uuid::from_string(idStr);
                    if (id.has_value())
                    {
                        char fourKBuffer[4096];
                        auto fourKBufferSize = sizeof(fourKBuffer);
                        auto requiredBufferSize = fourKBufferSize;

                        if (mxlGetFlowDef(instance, idStr.c_str(), fourKBuffer, &requiredBufferSize) != MXL_STATUS_OK)
                        {
                            std::cerr << "ERROR" << ": "
                                      << "Failed to get flow definition for flow id " << idStr << std::endl;
                            continue;
                        }

                        // parse the flow details.
                        auto flowDef = std::string{fourKBuffer, requiredBufferSize - 1};
                        auto const [label, groupName, roleInGroup] = getFlowDetails(flowDef);

                        // add the flow details to the appropriate group.
                        groups[groupName].emplace_back(*id, label, roleInGroup);
                    }
                }
            }
        }

        // Print the groups and their associated flow ids and labels.  Display the everything in a tree structure based on the group name, and show
        // the role in group as well.
        for (auto const& [groupName, groupInfo] : groups)
        {
            auto flowIds = std::vector<uuids::uuid>{};
            for (auto const& [flowId, label, roleInGroup] : groupInfo)
            {
                flowIds.push_back(flowId);
            }

            auto const mxlAddress = generateMxlAddress(in_domain, flowIds);
            auto hasRoleInGroupConflicts = false;
            auto seenRolesInGroup = std::set<std::string>{};

            // A group name is considered invalid if empty
            bool invalidGroup = groupName.empty();

            // Print the group name and mxl address for the group.
            // Pretty print if we are in a terminal, otherwise just print plain text.
            if (detail::isTerminal(std::cout))
            {
                auto color = invalidGroup ? fmt::color::red : fmt::color::white;
                std::cout << fmt::format(fmt::fg(color), "{}: ", groupName) << mxlAddress << std::endl;
            }
            else
            {
                if (invalidGroup)
                {
                    std::cout << "Invalid group name (empty string)";
                }
                else
                {
                    std::cout << groupName;
                }
                std::cout << ": " << mxlAddress << std::endl;
            }

            // Iterate over the flows in the group and print their details.
            // Also check for any role in group conflicts (i.e. multiple flows with the same non-empty role in group or empty role in group)
            // and flag those in the output.
            for (auto const& [flowId, label, roleInGroup] : groupInfo)
            {
                if (!roleInGroup.empty())
                {
                    if (seenRolesInGroup.contains(roleInGroup))
                    {
                        hasRoleInGroupConflicts = true;
                    }
                    else
                    {
                        seenRolesInGroup.insert(roleInGroup);
                    }
                }

                if (detail::isTerminal(std::cout))
                {
                    auto color = fmt::color::green;
                    if (hasRoleInGroupConflicts || roleInGroup.empty())
                    {
                        color = fmt::color::red;
                    }
                    else
                    {
                        color = fmt::color::white;
                    }

                    std::cout
                        << fmt::format(fmt::fg(color),
                               "\tID: {}, Label: {}, Role in Group: {}",
                               uuids::to_string(flowId),
                               label,
                               roleInGroup.empty() ? "MISSING" : roleInGroup)
                        << std::endl;
                }
                else
                {
                    std::cout << "\tID: " << uuids::to_string(flowId) << ", Label: " << label
                              << ", Role in Group: " << (roleInGroup.empty() ? "MISSING" : roleInGroup) << std::endl;
                }
            }
        }

        // Clean up the MXL instance before exiting.
        if ((instance != nullptr) && (mxlDestroyInstance(instance)) != MXL_STATUS_OK)
        {
            std::cerr << "ERROR" << ": "
                      << "Failed to destroy MXL instance." << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << std::endl;
        return EXIT_SUCCESS;
    }

    /// Print detailed information about a specific flow in the MXL domain, including its configuration and runtime status.  Also print the latency of
    /// the flow in grains/samples.
    /// \param in_domain The MXL domain the flow belongs to.
    /// \param in_id The id of the flow to print information about.
    /// \return EXIT_SUCCESS if the operation was successful, EXIT_FAILURE otherwise.
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

    template<typename F>
    int tryRun(F&& f)
    {
        try
        {
            return f();
        }
        catch (std::exception const& ex)
        {
            std::cerr << "ERROR: Caught exception: " << ex.what() << std::endl;
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
        status = tryRun([&]() { return garbageCollect(domain); });
    }
    // If list all is specified or if we don't have a flow id specified through option or URI: list all flows.
    else if (listOpt->count() > 0 || flowId.empty())
    {
        status = tryRun([&]() { return listAllFlows(domain); });
    }
    // The user specified a flow id (through the URI or command line option): print info for that flow.
    else if (!flowId.empty())
    {
        status = tryRun([&]() { return printFlow(domain, flowId); });
    }
    else
    {
        std::cerr << "No action specified. Use --help for usage information." << std::endl;
        status = EXIT_FAILURE;
    }

    return status;
}
