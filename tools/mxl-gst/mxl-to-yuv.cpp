// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <CLI/CLI.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "../../lib/src/internal/FlowParser.hpp"
#include "../../lib/src/internal/Logging.hpp"
#include "../../lib/src/internal/PathUtils.hpp"

#ifdef __APPLE__
#   include <TargetConditionals.h>
#endif

#ifdef __APPLE__
#   include <TargetConditionals.h>
#endif

std::sig_atomic_t volatile g_exit_requested = 0;

void signal_handler(int)
{
    g_exit_requested = 1;
}

int main(int argc, char** argv)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

    CLI::App app("mxl-gst-videosink");

    std::string flowID;
    auto flowIDOpt = app.add_option("-f, --flow-id", flowID, "The flow ID");
    flowIDOpt->required(true);

    std::string domain;
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    CLI11_PARSE(app, argc, argv);

    // So the source need to generate a json?
    auto const descriptor_path = mxl::lib::makeFlowDescriptorFilePath(domain, flowID);
    if (!std::filesystem::exists(descriptor_path))
    {
        MXL_ERROR("Flow descriptor file '{}' does not exist", descriptor_path.string());
        return EXIT_FAILURE;
    }

    std::ifstream descriptor_reader(descriptor_path);
    std::string flow_descriptor((std::istreambuf_iterator<char>(descriptor_reader)), std::istreambuf_iterator<char>());
    mxl::lib::FlowParser descriptor_parser{flow_descriptor};

    int exit_status = EXIT_SUCCESS;
    mxlStatus ret;

    Rational rate;
    std::uint64_t editUnitDurationNs = 33'000'000LL;
    std::uint64_t grain_index = 0;

    auto instance = mxlCreateInstance(domain.c_str(), "");
    if (instance == nullptr)
    {
        MXL_ERROR("Failed to create MXL instance");
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    // Create a flow reader for the given flow id.
    mxlFlowReader reader;
    ret = mxlCreateFlowReader(instance, flowID.c_str(), "", &reader);
    if (ret != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create flow reader with status '{}'", static_cast<int>(ret));
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    // Extract the FlowInfo structure.
    FlowInfo flow_info;
    ret = mxlFlowReaderGetInfo(reader, &flow_info);
    if (ret != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to get flow info with status '{}'", static_cast<int>(ret));
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    rate = flow_info.discrete.grainRate;
    editUnitDurationNs = static_cast<std::uint64_t>(1.0 * rate.denominator * (1'000'000'000.0 / rate.numerator));
    editUnitDurationNs += 1'000'000ULL; // allow some margin.

    grain_index = mxlGetCurrentIndex(&flow_info.discrete.grainRate);
    while (!g_exit_requested)
    {
        GrainInfo grain_info;
        uint8_t* payload;
        auto ret = mxlFlowReaderGetGrain(reader, grain_index, editUnitDurationNs, &grain_info, &payload);
        if (ret != MXL_STATUS_OK)
        {
            // Missed a grain. resync.
            MXL_ERROR("Missed grain {}, err : {}", grain_index, (int)ret);
            grain_index = mxlGetCurrentIndex(&flow_info.discrete.grainRate);
            continue;
        }
        else if (grain_info.commitedSize != grain_info.grainSize)
        {
            // we don't need partial grains. wait for the grain to be complete.
            continue;
        }

        grain_index++;

        // write grain to a yuv file
    }

mxl_cleanup:
    if (instance != nullptr)
    {
        // clean-up mxl objects
        mxlReleaseFlowReader(instance, reader);
        mxlDestroyInstance(instance);
    }

    return exit_status;
}
