#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <uuid.h>
#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <gsl/span>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <sys/file.h>
#include "../../lib/src/internal/PathUtils.hpp"

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
}

std::ostream& operator<<(std::ostream& os, FlowInfo const& info)
{
    auto span = gsl::span<std::uint8_t, sizeof info.common.id>(const_cast<std::uint8_t*>(info.common.id), sizeof info.common.id);
    auto id = uuids::uuid(span);
    os << "- Flow [" << uuids::to_string(id) << ']' << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Version", info.version) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Struct size", info.size) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Last write time", info.common.lastWriteTime) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Last read time", info.common.lastReadTime) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Format", getFormatString(info.common.format)) << '\n'
       << '\t' << fmt::format("{: >18}: {:0>8x}", "Flags", info.common.flags) << '\n';

    if (mxlIsDiscreteDataFormat(info.common.format))
    {
        os << '\t' << fmt::format("{: >18}: {}/{}", "Grain rate", info.discrete.grainRate.numerator, info.discrete.grainRate.denominator) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Grain count", info.discrete.grainCount) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Head index", info.discrete.headIndex) << '\n';
    }
    else if (mxlIsContinuousDataFormat(info.common.format))
    {
        os << '\t' << fmt::format("{: >18}: {}/{}", "Sample rate", info.continuous.sampleRate.numerator, info.continuous.sampleRate.denominator)
           << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Channel count", info.continuous.channelCount) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Buffer length", info.continuous.bufferLength) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Commit batch size", info.continuous.commitBatchSize) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Sync batch size", info.continuous.syncBatchSize) << '\n'
           << '\t' << fmt::format("{: >18}: {}", "Head index", info.continuous.headIndex) << '\n';
    }

    auto const now = mxlGetTime();
    auto const currentIndex = mxlTimestampToIndex(&info.discrete.grainRate, now);
    os << '\t' << fmt::format("{: >18}: {}", "Latency (grains)", currentIndex - info.discrete.headIndex) << '\n'
       << '\t' << fmt::format("{: >18}: {}", "Latency (ns)", now - info.common.lastWriteTime) << std::endl;

    return os;
}

int listAllFlows(std::string const& in_domain)
{
    std::cout << "--- MXL Flows ---" << '\n';
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
                    std::cout << "\t" << *id << '\n';
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

    // Extract the FlowInfo structure.
    FlowInfo info;
    status = mxlFlowReaderGetInfo(reader, &info);
    if (status != MXL_STATUS_OK)
    {
        std::cerr << "Failed to get flow info" << std::endl;
        ret = EXIT_FAILURE;
    }
    else
    {
        // Print the flow information.
        std::cout << info;

        // Try to obtain an exclusive lock on the flow data file.  If we can obtain one it means that no
        // other process is writing to the flow.
        auto flowDataFile = mxl::lib::makeFlowDataFilePath(in_domain, in_id);
        int fd = open(flowDataFile.c_str(), O_RDONLY | O_CLOEXEC);

        // Try to obtain an exclusive lock on the file descriptor. Do not block if the lock cannot be obtained.
        bool active = flock(fd, LOCK_EX | LOCK_NB) < 0;
        close(fd);

        // Print the status of the flow.
        std::cout << '\t' << fmt::format("{: >18}: {}", "Active", active) << std::endl;

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