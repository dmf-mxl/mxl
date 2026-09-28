// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file cuda_reader_helper.cpp
 *
 * Reads one grain of a flow whose payload is in CUDA device memory, from a process other than the writer's, and
 * checks that every payload byte has an expected value. Used by test_cuda_payload.cpp to test CUDA IPC between
 * processes.
 *
 * Usage: mxl-cuda-reader-helper \<domain\> \<flow id\> \<grain index\> \<expected byte\> [reader options]
 * Exit status: 0 if the grain was read and every byte matched, 1 otherwise. Errors are printed to stderr.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <cuda_runtime_api.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>

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
        (void)std::fprintf(stderr, "mxl-cuda-reader-helper: %s failed with status %d\n", what, status);
        return 1;
    }
}

/**
 * \param[in] argc The number of arguments.
 * \param[in] argv The arguments, see the file description.
 * \return 0 on success, 1 on failure.
 */
int main(int argc, char** argv)
{
    if ((argc != 5) && (argc != 6))
    {
        (void)std::fprintf(stderr, "usage: %s <domain> <flow id> <grain index> <expected byte> [reader options]\n", argv[0]);
        return 1;
    }
    auto const* const domain = argv[1];
    auto const* const flowId = argv[2];
    auto const index = std::strtoull(argv[3], nullptr, 10);
    auto const expected = static_cast<std::uint8_t>(std::strtoul(argv[4], nullptr, 10));
    auto const* const options = (argc == 6) ? argv[5] : "";

    auto instance = mxlCreateInstance(domain, "");
    if (instance == nullptr)
    {
        return fail("mxlCreateInstance", -1);
    }

    mxlFlowReader reader = nullptr;
    if (auto const status = mxlCreateFlowReader(instance, flowId, options, &reader); status != MXL_STATUS_OK)
    {
        return fail("mxlCreateFlowReader", status);
    }

    auto layout = mxlGrainStorageLayout{};
    if (auto const status = mxlFlowReaderGetStorageLayout(reader, &layout); status != MXL_STATUS_OK)
    {
        return fail("mxlFlowReaderGetStorageLayout", status);
    }
    if (layout.storageType != MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER)
    {
        return fail("checking the storage type", static_cast<int>(layout.storageType));
    }

    auto slots = std::vector<mxlGrainStorage>(layout.slotCount);
    if (auto const status = mxlFlowReaderMapSlots(reader, layout.slotCount, slots.data()); status != MXL_STATUS_OK)
    {
        return fail("mxlFlowReaderMapSlots", status);
    }

    auto grainInfo = mxlGrainInfo{};
    auto slot = std::uint32_t{0};
    if (auto const status = mxlFlowReaderGetGrainSlot(reader, index, MXL_GRAIN_VALID_SLICES_ALL, 1'000'000'000ULL, &grainInfo, &slot);
        status != MXL_STATUS_OK)
    {
        return fail("mxlFlowReaderGetGrainSlot", status);
    }

    auto bytes = std::vector<std::uint8_t>(grainInfo.grainSize);
    if (auto const error = ::cudaMemcpy(bytes.data(), slots[slot].cuda.pointer, bytes.size(), cudaMemcpyDeviceToHost); error != cudaSuccess)
    {
        return fail("cudaMemcpy", static_cast<int>(error));
    }
    for (auto const byte : bytes)
    {
        if (byte != expected)
        {
            return fail("checking the payload", byte);
        }
    }

    mxlReleaseFlowReader(instance, reader);
    mxlDestroyInstance(instance);
    return 0;
}
