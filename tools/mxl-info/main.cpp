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
            constexpr explicit LatencyPrinter(mxlFlowInfo const& info) noexcept
                : flowInfo{&info}
            {}

            mxlFlowInfo const* flowInfo;
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

        void outputLatency(std::ostream& os, std::uint64_t headIndex, mxlRational const& grainRate, std::uint64_t limit)
        {
            auto const now = mxlGetTime();

            auto const currentIndex = mxlTimestampToIndex(&grainRate, now);
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

            if (mxlIsDiscreteDataFormat(lp.flowInfo->config.common.format))
            {
                outputLatency(os, lp.flowInfo->runtime.headIndex, lp.flowInfo->config.common.grainRate, lp.flowInfo->config.discrete.grainCount);
            }
            else if (mxlIsContinuousDataFormat(lp.flowInfo->config.common.format))
            {
                outputLatency(os, lp.flowInfo->runtime.headIndex, lp.flowInfo->config.common.grainRate, lp.flowInfo->config.continuous.bufferLength);
            }
            return os;
        }
    }

    detail::LatencyPrinter formatWithLatency(mxlFlowInfo const& info)
    {
        return detail::LatencyPrinter{info};
    }

    void getFlowDetails(std::filesystem::path const& descPath, std::string& label, std::string& groupHint)
    {
        label = std::string{"n/a"};
        groupHint = std::string{"n/a"};

        if (!exists(descPath))
        {
            return;
        }

        auto descFile = std::ifstream{descPath};
        if (!descFile)
        {
            return;
        }

        auto const content = std::string((std::istreambuf_iterator<char>(descFile)), std::istreambuf_iterator<char>());
        descFile.close();

        auto v = picojson::value{};
        std::string err = picojson::parse(v, content);
        if (!err.empty())
        {
            return;
        }

        auto const& obj = v.get<picojson::object>();
        auto const flowTypeIt = obj.find("label");
        if ((flowTypeIt == obj.end()) || !flowTypeIt->second.is<std::string>())
        {
            return;
        }
        else
        {
            label = flowTypeIt->second.get<std::string>();
        }

        // try to get the group hint tag
        auto const tagsIt = obj.find("tags");
        if ((tagsIt != obj.end()) && tagsIt->second.is<picojson::object>())
        {
            auto const& tagsObj = tagsIt->second.get<picojson::object>();
            auto const groupHintIt = tagsObj.find("urn:x-nmos:tag:grouphint/v1.0");
            if ((groupHintIt != tagsObj.end()) && groupHintIt->second.is<picojson::array>())
            {
                auto const& groupHintArray = groupHintIt->second.get<picojson::array>();
                if (!groupHintArray.empty() && groupHintArray[0].is<std::string>())
                {
                    groupHint = groupHintArray[0].get<std::string>();
                }
            }
        }
    }

    int listAllFlows(std::string const& in_domain)
    {
        auto base = std::filesystem::path{in_domain};

        if (exists(base) && is_directory(base))
        {
            for (auto const& entry : std::filesystem::directory_iterator{base})
            {
                if (is_directory(entry) && (entry.path().extension() == mxl::lib::FLOW_DIRECTORY_NAME_SUFFIX))
                {
                    // this looks like a uuid. try to parse it an confirm it is valid.
                    auto id = uuids::uuid::from_string(entry.path().stem().string());
                    if (id.has_value())
                    {
                        auto descPath = mxl::lib::makeFlowDescriptorFilePath(base, entry.path().stem().string());
                        std::string label;
                        std::string groupHint;

                        try
                        {
                            getFlowDetails(descPath, label, groupHint);
                        }
                        catch (std::exception const& e)
                        {
                            label = fmt::format("ERROR: {}", e.what());
                            groupHint = "n/a";
                        }
                        // Output CSV format: id,label,group_hint
                        std::cout << *id << ", \"" << label << "\", \"" << groupHint << "\"" << std::endl;
                    }
                }
            }
        }

        std::cout << std::endl;
        return EXIT_SUCCESS;
    }

    int printFlow(std::string const& in_domain, std::string const& in_id)
    {
        int ret = EXIT_SUCCESS;

        // Create the SDK instance with a specific domain.
        auto instance = mxlCreateInstance(in_domain.c_str(), "");
        if (instance == nullptr)
        {
            std::cerr << "Failed to create MXL instance" << std::endl;
            return EXIT_FAILURE;
        }

        // Create a flow reader for the given flow id.
        mxlFlowReader reader;
        mxlStatus status = mxlCreateFlowReader(instance, in_id.c_str(), "", &reader);
        if (status != MXL_STATUS_OK)
        {
            std::cerr << "Failed to create flow reader" << std::endl;
            mxlDestroyInstance(instance);
            return EXIT_FAILURE;
        }

        // Extract the mxlFlowInfo structure.
        mxlFlowInfo info;
        status = mxlFlowReaderGetInfo(reader, &info);
        if (status != MXL_STATUS_OK)
        {
            std::cerr << "Failed to get flow info" << std::endl;
            ret = EXIT_FAILURE;
        }
        else
        {
            std::cout << formatWithLatency(info);

            auto active = false;
            status = mxlIsFlowActive(instance, in_id.c_str(), &active);
            if (status != MXL_STATUS_OK)
            {
                std::cerr << "Failed to check if flow is active: " << status << std::endl;
            }
            else
            {
                // Print the status of the flow.
                std::cout << '\t' << fmt::format("{: >18}: {}", "Active", active) << std::endl;
            }

            ret = EXIT_SUCCESS;
        }

        // Cleanup
        mxlReleaseFlowReader(instance, reader);
        mxlDestroyInstance(instance);

        return ret;
    }

    // Perform garbage collection on the MXL domain.
    int garbageCollect(std::string const& in_domain)
    {
        // Create the SDK instance with a specific domain.
        auto instance = mxlCreateInstance(in_domain.c_str(), "");
        if (instance == nullptr)
        {
            std::cerr << "Failed to create MXL instance" << std::endl;
            return EXIT_FAILURE;
        }

        auto status = mxlGarbageCollectFlows(instance);
        if (status != MXL_STATUS_OK)
        {
            std::cerr << "Failed to perform garbage collection : " << status << std::endl;
            mxlDestroyInstance(instance);
            return EXIT_FAILURE;
        }

        mxlDestroyInstance(instance);

        return EXIT_SUCCESS;
    }
}

int main(int argc, char** argv)
{
    CLI::App app("mxl-info");

    mxlVersionType version;
    mxlGetVersion(&version);

    std::stringstream ss;
    ss << version.major << "." << version.minor << "." << version.bugfix << "." << version.build;

    // add version output
    app.set_version_flag("--version", ss.str());

    std::string domain = "";
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    std::string flowId = "";
    auto flowIdOpt = app.add_option("-f,--flow", flowId, "The flow id to analyse");

    auto allOpt = app.add_flag("-l,--list", "List all flows in the MXL domain");
    auto gcOpt = app.add_flag("-g,--garbage-collect", "Garbage collect inactive flows found in the MXL domain");

    CLI11_PARSE(app, argc, argv);

    int status = EXIT_SUCCESS;

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
