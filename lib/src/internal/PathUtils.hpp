// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>

namespace mxl::lib
{
    constexpr auto const FLOW_DIRECTORY_NAME_SUFFIX = ".mxl-flow";
    constexpr auto const FLOW_DESCRIPTOR_FILE_NAME = "flow_def.json";
    constexpr auto const FLOW_DATA_FILE_NAME = "data";
    constexpr auto const FLOW_ACCESS_FILE_NAME = "access";
    constexpr auto const GRAIN_DIRECTORY_NAME = "grains";
    constexpr auto const GRAIN_DATA_FILE_NAME_STEM = "data";
    constexpr auto const CHANNEL_DATA_FILE_NAME = "channels";
    constexpr auto const DOMAIN_OPTIONS_FILE_NAME = "options.json";

    std::filesystem::path makeFlowDirectoryName(std::filesystem::path const& domain, std::string const& uuid);

    std::filesystem::path makeFlowDataFilePath(std::filesystem::path const& flowDirectory);
    std::filesystem::path makeFlowDataFilePath(std::filesystem::path const& domain, std::string const& uuid);

    std::filesystem::path makeFlowDescriptorFilePath(std::filesystem::path const& flowDirectory);
    std::filesystem::path makeFlowDescriptorFilePath(std::filesystem::path const& domain, std::string const& uuid);

    std::filesystem::path makeFlowAccessFilePath(std::filesystem::path const& flowDirectory);
    std::filesystem::path makelowAccessFilePath(std::filesystem::path const& domain, std::string const& uuid);

    std::filesystem::path makeGrainDirectoryName(std::filesystem::path const& flowDirectory);
    std::filesystem::path makeGrainDirectoryName(std::filesystem::path const& domain, std::string const& uuid);

    std::filesystem::path makeGrainDataFilePath(std::filesystem::path const& grainDirectory, unsigned int index);
    std::filesystem::path makeGrainDataFilePath(std::filesystem::path const& domain, std::string const& uuid, unsigned int index);

    std::filesystem::path makeChannelDataFilePath(std::filesystem::path const& flowDirectory);
    std::filesystem::path makeChannelDataFilePath(std::filesystem::path const& domain, std::string const& uuid);

    std::filesystem::path makeDomainOptionsFilePath(std::filesystem::path const& domain);

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    inline std::filesystem::path makeFlowDataFilePath(std::filesystem::path const& domain, std::string const& uuid)
    {
        return makeFlowDataFilePath(makeFlowDirectoryName(domain, uuid));
    }

    inline std::filesystem::path makeFlowDescriptorFilePath(std::filesystem::path const& domain, std::string const& uuid)
    {
        return makeFlowDescriptorFilePath(makeFlowDirectoryName(domain, uuid));
    }

    inline std::filesystem::path makeFlowAccessFilePath(std::filesystem::path const& domain, std::string const& uuid)
    {
        return makeFlowAccessFilePath(makeFlowDirectoryName(domain, uuid));
    }

    inline std::filesystem::path makeGrainDirectoryName(std::filesystem::path const& domain, std::string const& uuid)
    {
        return makeGrainDirectoryName(makeFlowDirectoryName(domain, uuid));
    }

    inline std::filesystem::path makeGrainDataFilePath(std::filesystem::path const& domain, std::string const& uuid, unsigned int index)
    {
        return makeGrainDataFilePath(makeGrainDirectoryName(domain, uuid), index);
    }

    inline std::filesystem::path makeChannelDataFilePath(std::filesystem::path const& domain, std::string const& uuid)
    {
        return makeChannelDataFilePath(makeFlowDirectoryName(domain, uuid));
    }
}
