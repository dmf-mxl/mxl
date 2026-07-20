// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/PathUtils.hpp"
#include <fmt/format.h>
#include <mxl/platform.h>

namespace mxl::lib
{
    MXL_EXPORT
    std::filesystem::path makeFlowDirectoryName(std::filesystem::path const& domain, std::string const& uuid)
    {
        return domain / (uuid + FLOW_DIRECTORY_NAME_SUFFIX);
    }

    MXL_EXPORT
    std::filesystem::path makeFlowDataFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_DATA_FILE_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeFlowDescriptorFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_DESCRIPTOR_FILE_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeFlowAccessFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_ACCESS_FILE_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeGrainDirectoryName(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / GRAIN_DIRECTORY_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeGrainDataFilePath(std::filesystem::path const& grainDirectory, unsigned int index)
    {
        return grainDirectory / fmt::format("{}.{}", GRAIN_DATA_FILE_NAME_STEM, index);
    }

    MXL_EXPORT
    std::filesystem::path makeChannelDataFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / CHANNEL_DATA_FILE_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeDomainOptionsFilePath(std::filesystem::path const& domain)
    {
        return domain / (DOMAIN_OPTIONS_FILE_NAME);
    }
}
