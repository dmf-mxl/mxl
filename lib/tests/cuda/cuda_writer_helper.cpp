// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file cuda_writer_helper.cpp
 *
 * Creates one flow whose payload is in CUDA device memory, writes one grain, and waits until it is signaled.
 * A reader in another process, including one in another container that shares this process's IPC namespace, can
 * import the grain while this process is still running.
 *
 * Usage: mxl-cuda-writer-helper \<domain\> \<flow definition\> \<byte\> [writer options]
 * On success it prints "READY \<grain index\>" to stdout and waits for SIGTERM or SIGINT.
 * Exit status: 0 after a signal, 1 if the flow could not be created. Errors are printed to stderr.
 */

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <string>
#include <vector>
#include <cuda_runtime_api.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "CudaTestUtils.hpp"

namespace
{
    /**
     * Print an error and return the failure exit status.
     * \param[in] what The failed step.
     * \param[in] status The status it returned.
     * \return 1.
     */
    int fail(char const* what, int status)
    {
        (void)std::fprintf(stderr, "mxl-cuda-writer-helper: %s failed with status %d\n", what, status);
        return 1;
    }

    /**
     * \param[in] path A file.
     * \return Its contents, or an empty string if it could not be read.
     */
    std::string readFile(std::filesystem::path const& path)
    {
        auto in = std::ifstream{path};
        auto buffer = std::stringstream{};
        buffer << in.rdbuf();
        return buffer.str();
    }
}

/**
 * \param[in] argc The number of arguments.
 * \param[in] argv The arguments, see the file description.
 * \return 0 after SIGTERM or SIGINT, 1 on failure.
 */
int main(int argc, char** argv)
{
    if ((argc < 4) || (argc > 5))
    {
        (void)std::fprintf(stderr, "usage: %s <domain> <flow definition> <byte> [writer options]\n", argv[0]);
        return 1;
    }

    // Block the stop signals before any work, so a signal cannot arrive between READY and the wait.
    auto signals = sigset_t{};
    sigemptyset(&signals);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    if (sigprocmask(SIG_BLOCK, &signals, nullptr) != 0)
    {
        return fail("sigprocmask", errno);
    }

    auto const* const domain = argv[1];
    auto const flowDef = readFile(argv[2]);
    if (flowDef.empty())
    {
        return fail("reading the flow definition", 1);
    }
    auto const value = static_cast<std::uint8_t>(std::strtoul(argv[3], nullptr, 10));
    auto options = std::string{};
    if (argc == 5)
    {
        options = argv[4];
    }
    else
    {
        auto const uuid = mxl::tests::cuda::deviceUuid(0);
        if (uuid.empty())
        {
            return fail("finding a CUDA device", 0);
        }
        options = std::string{R"({"payload":{"backend":"cuda-linear","deviceUuid":")"} + uuid + R"("}})";
    }

    std::error_code ec;
    std::filesystem::create_directories(domain, ec);
    if (ec)
    {
        return fail("creating the domain", ec.value());
    }

    auto instance = mxlCreateInstance(domain, "");
    if (instance == nullptr)
    {
        return fail("mxlCreateInstance", -1);
    }
    mxlFlowWriter writer = nullptr;
    auto configInfo = mxlFlowConfigInfo{};
    if (auto const status = mxlCreateFlowWriter(instance, flowDef.c_str(), options.c_str(), &writer, &configInfo, nullptr);
        status != MXL_STATUS_OK)
    {
        return fail("mxlCreateFlowWriter", status);
    }

    auto layout = mxlGrainStorageLayout{};
    if (auto const status = mxlFlowWriterGetStorageLayout(writer, &layout); status != MXL_STATUS_OK)
    {
        return fail("mxlFlowWriterGetStorageLayout", status);
    }
    if (auto const error = ::cudaSetDevice(layout.deviceIndex); error != cudaSuccess)
    {
        return fail("cudaSetDevice", static_cast<int>(error));
    }
    auto slots = std::vector<mxlGrainStorage>(layout.slotCount);
    if (auto const status = mxlFlowWriterMapSlots(writer, layout.slotCount, slots.data()); status != MXL_STATUS_OK)
    {
        return fail("mxlFlowWriterMapSlots", status);
    }

    auto const index = mxlGetCurrentIndex(&configInfo.common.grainRate);
    auto grainInfo = mxlGrainInfo{};
    auto slot = std::uint32_t{0};
    if (auto const status = mxlFlowWriterOpenGrainSlot(writer, index, &grainInfo, &slot); status != MXL_STATUS_OK)
    {
        return fail("mxlFlowWriterOpenGrainSlot", status);
    }
    if (auto const error = ::cudaMemset(slots[slot].cuda.pointer, value, grainInfo.grainSize); error != cudaSuccess)
    {
        return fail("cudaMemset", static_cast<int>(error));
    }
    if (auto const error = ::cudaDeviceSynchronize(); error != cudaSuccess)
    {
        return fail("cudaDeviceSynchronize", static_cast<int>(error));
    }
    grainInfo.validSlices = grainInfo.totalSlices;
    if (auto const status = mxlFlowWriterCommitGrain(writer, &grainInfo); status != MXL_STATUS_OK)
    {
        return fail("mxlFlowWriterCommitGrain", status);
    }

    (void)std::fprintf(stdout, "READY %llu\n", static_cast<unsigned long long>(index));
    if (std::fflush(stdout) != 0)
    {
        return fail("flushing READY", errno);
    }

    auto sig = 0;
    if (sigwait(&signals, &sig) != 0)
    {
        return fail("sigwait", errno);
    }

    mxlReleaseFlowWriter(instance, writer);
    mxlDestroyInstance(instance);
    return 0;
}
