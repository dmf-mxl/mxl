// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <cuda_runtime_api.h>

namespace mxl::tests::cuda
{
    /**
     * \param[in] ordinal A CUDA device index in this process.
     * \return The UUID of the device in the format nvidia-smi prints, or an empty string if the device does not exist.
     */
    inline std::string deviceUuid(int ordinal)
    {
        auto properties = cudaDeviceProp{};
        if (::cudaGetDeviceProperties(&properties, ordinal) != cudaSuccess)
        {
            (void)::cudaGetLastError();
            return {};
        }

        constexpr auto HEX = "0123456789abcdef";
        auto result = std::string{"GPU-"};
        for (auto i = 0; i < 16; ++i)
        {
            if ((i == 4) || (i == 6) || (i == 8) || (i == 10))
            {
                result.push_back('-');
            }
            auto const byte = static_cast<std::uint8_t>(properties.uuid.bytes[i]);
            result.push_back(HEX[byte >> 4U]);
            result.push_back(HEX[byte & 0x0FU]);
        }
        return result;
    }

    /**
     * \return The number of CUDA devices visible to this process, 0 if CUDA is not usable.
     */
    inline int deviceCount()
    {
        auto count = 0;
        if (::cudaGetDeviceCount(&count) != cudaSuccess)
        {
            (void)::cudaGetLastError();
            return 0;
        }
        return count;
    }
}
