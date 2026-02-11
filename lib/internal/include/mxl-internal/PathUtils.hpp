// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file PathUtils.hpp
 * @brief Filesystem path construction for MXL domain and flow files
 *
 * MXL stores flows in a well-defined directory structure on tmpfs (or regular filesystem):
 *
 *   ${domain}/
 *     ${flowId}.mxl-flow/          -- One directory per flow, named by UUID
 *       flow_def.json              -- Flow metadata (format, dimensions, frame rate, etc.)
 *       data                       -- FlowState shared memory file (ring buffer metadata)
 *       access                     -- Reserved for future access control
 *       grains/                    -- For discrete flows (VIDEO/DATA)
 *         data.0, data.1, ...      -- Individual grain (frame) memory-mapped files
 *       channels                   -- For continuous flows (AUDIO) - per-channel sample ring buffers
 *
 * This file provides utility functions to construct these paths consistently across the codebase.
 * All functions are inline for zero overhead. The overloaded versions allow constructing paths
 * from either a flow directory or from domain+UUID, reducing boilerplate in calling code.
 */

#pragma once

#include <filesystem>

namespace mxl::lib
{
    // File and directory name constants defining the MXL filesystem layout
    constexpr auto const FLOW_DIRECTORY_NAME_SUFFIX = ".mxl-flow";     // Suffix for flow directories (e.g., "abc-123.mxl-flow")
    constexpr auto const FLOW_DESCRIPTOR_FILE_NAME = "flow_def.json";  // JSON file with flow metadata
    constexpr auto const FLOW_DATA_FILE_NAME = "data";                 // Shared memory file for FlowState (ring buffer header)
    constexpr auto const FLOW_ACCESS_FILE_NAME = "access";             // Reserved for future access control features
    constexpr auto const GRAIN_DIRECTORY_NAME = "grains";              // Subdirectory containing individual grain files (discrete flows)
    constexpr auto const GRAIN_DATA_FILE_NAME_STEM = "data";           // Stem for grain files: "data.0", "data.1", etc.
    constexpr auto const CHANNEL_DATA_FILE_NAME = "channels";          // Shared memory file for audio sample ring buffers (continuous flows)
    constexpr auto const DOMAIN_OPTIONS_FILE_NAME = "options.json";    // Domain-wide configuration file

    /**
     * Construct the flow directory name from domain and flow UUID.
     * Example: makeFlowDirectoryName("/dev/shm/mxl", "abc-123") -> "/dev/shm/mxl/abc-123.mxl-flow"
     */
    std::filesystem::path makeFlowDirectoryName(std::filesystem::path const& domain, std::string const& uuid);

    /** Construct path to the FlowState data file (ring buffer metadata) from flow directory. */
    std::filesystem::path makeFlowDataFilePath(std::filesystem::path const& flowDirectory);
    /** Construct path to the FlowState data file from domain and UUID. */
    std::filesystem::path makeFlowDataFilePath(std::filesystem::path const& domain, std::string const& uuid);

    /** Construct path to the flow descriptor JSON file from flow directory. */
    std::filesystem::path makeFlowDescriptorFilePath(std::filesystem::path const& flowDirectory);
    /** Construct path to the flow descriptor JSON file from domain and UUID. */
    std::filesystem::path makeFlowDescriptorFilePath(std::filesystem::path const& domain, std::string const& uuid);

    /** Construct path to the access control file from flow directory. */
    std::filesystem::path makeFlowAccessFilePath(std::filesystem::path const& flowDirectory);
    /** Construct path to the access control file from domain and UUID. */
    std::filesystem::path makelowAccessFilePath(std::filesystem::path const& domain, std::string const& uuid);

    /** Construct path to the grains subdirectory from flow directory. */
    std::filesystem::path makeGrainDirectoryName(std::filesystem::path const& flowDirectory);
    /** Construct path to the grains subdirectory from domain and UUID. */
    std::filesystem::path makeGrainDirectoryName(std::filesystem::path const& domain, std::string const& uuid);

    /**
     * Construct path to a specific grain data file from grain directory and index.
     * Example: makeGrainDataFilePath("/path/grains", 5) -> "/path/grains/data.5"
     */
    std::filesystem::path makeGrainDataFilePath(std::filesystem::path const& grainDirectory, unsigned int index);
    /** Construct path to a specific grain data file from domain, UUID, and index. */
    std::filesystem::path makeGrainDataFilePath(std::filesystem::path const& domain, std::string const& uuid, unsigned int index);

    /** Construct path to the audio channels data file from flow directory. */
    std::filesystem::path makeChannelDataFilePath(std::filesystem::path const& flowDirectory);
    /** Construct path to the audio channels data file from domain and UUID. */
    std::filesystem::path makeChannelDataFilePath(std::filesystem::path const& domain, std::string const& uuid);

    /** Construct path to the domain-wide options JSON file. */
    std::filesystem::path makeDomainOptionsFilePath(std::filesystem::path const& domain);

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /* These convenience overloads delegate to the primary implementations,   */
    /* reducing code duplication in calling sites.                            */
    /**************************************************************************/

    /**
     * Inline convenience: construct flow data file path from domain+UUID.
     * Delegates to the flow directory version after constructing the directory path.
     */
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
