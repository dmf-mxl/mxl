// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/FlowInfo.hpp"
#include "mxl-internal/PathUtils.hpp"

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

    std::string readFile(std::string const& filepath)
    {
        if (auto file = std::ifstream{filepath, std::ios::in | std::ios::binary}; file)
        {
            auto reader = std::stringstream{};
            reader << file.rdbuf();
            return reader.str();
        }

        throw std::runtime_error{"Failed to open file: " + filepath};
    }

    std::pair<std::string, std::string> getFlowDetails(std::filesystem::path const& descPath) noexcept
    {
        auto result = std::pair<std::string, std::string>{"n/a", "n/a"};

        try
        {
            auto const content = readFile(descPath);

            auto v = picojson::value{};
            if (auto const errorString = picojson::parse(v, content); errorString.empty())
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
        if (auto const base = std::filesystem::path{in_domain}; is_directory(base))
        {
            for (auto const& entry : std::filesystem::directory_iterator{base})
            {
                if (is_directory(entry) && (entry.path().extension() == mxl::lib::FLOW_DIRECTORY_NAME_SUFFIX))
                {
                    // this looks like a uuid. try to parse it an confirm it is valid.
                    auto const id = uuids::uuid::from_string(entry.path().stem().string());
                    if (id.has_value())
                    {
                        auto const descPath = mxl::lib::makeFlowDescriptorFilePath(base, entry.path().stem().string());
                        auto const [label, groupHint] = getFlowDetails(descPath);

                        // Output CSV format: id,label,group_hint
                        std::cout << *id << ", " << std::quoted(label) << ", " << std::quoted(groupHint) << '\n';
                    }
                }
            }
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

    auto version = ::mxlVersionType{};
    ::mxlGetVersion(&version);

    {
        auto ss = std::stringstream{};
        ss << version.major << '.' << version.minor << '.' << version.bugfix << '.' << version.build;

        // add version output
        app.set_version_flag("--version", ss.str());
    }

    auto domain = std::string{};
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    auto flowId = std::string{};
    auto flowIdOpt = app.add_option("-f,--flow", flowId, "The flow id to analyse");

    auto allOpt = app.add_flag("-l,--list", "List all flows in the MXL domain");
    auto gcOpt = app.add_flag("-g,--garbage-collect", "Garbage collect inactive flows found in the MXL domain");

    CLI11_PARSE(app, argc, argv);

    auto status = EXIT_SUCCESS;
    if (gcOpt->count() > 0)
    {
        status = garbageCollect(domain);
    }
    else if (allOpt->count() > 0)
    {
        status = listAllFlows(domain);
    }
    else if (flowIdOpt->count() > 0)
    {
        status = printFlow(domain, flowId);
    }

    return status;
}
