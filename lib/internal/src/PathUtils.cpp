// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file PathUtils.cpp
 * @brief File path construction utilities for MXL domain filesystem layout
 *
 * This file implements the standard filesystem layout for MXL domains. Each domain
 * is a tmpfs directory containing flow subdirectories, which in turn contain the
 * shared memory files and metadata for each flow.
 *
 * Standard MXL domain layout:
 *   <domain>/
 *     ├── options.json                          # Domain-wide configuration
 *     ├── <flow-uuid>.mxl-flow/                 # Flow directory
 *     │   ├── data                              # Shared memory flow metadata (mmap'd)
 *     │   ├── flow_def.json                     # NMOS flow definition
 *     │   ├── access                            # Timestamp file for read tracking
 *     │   ├── grains/                           # For discrete flows (video/data)
 *     │   │   ├── grain.0                       # Ring buffer grain 0 (mmap'd)
 *     │   │   ├── grain.1                       # Ring buffer grain 1 (mmap'd)
 *     │   │   └── ...
 *     │   └── channels                          # For continuous flows (audio)
 *     │                                         # Per-channel ring buffers (mmap'd)
 *
 * All paths use the constants defined in PathUtils.hpp to ensure consistency
 * across the codebase.
 */

#include "mxl-internal/PathUtils.hpp"
#include <fmt/format.h>
#include <mxl/platform.h>

namespace mxl::lib
{
    /**
     * @brief Construct the path to a flow's directory within a domain
     *
     * Flow directories use the pattern: <domain>/<uuid>.mxl-flow/
     * The .mxl-flow suffix distinguishes flow directories from other files
     * in the domain (like options.json) and makes cleanup/discovery easier.
     *
     * @param domain The MXL domain path (typically a tmpfs mount)
     * @param uuid The flow UUID as a string
     * @return Full path to the flow directory
     */
    MXL_EXPORT
    std::filesystem::path makeFlowDirectoryName(std::filesystem::path const& domain, std::string const& uuid)
    {
        return domain / (uuid + FLOW_DIRECTORY_NAME_SUFFIX);
    }

    /**
     * @brief Construct the path to a flow's shared memory data file
     *
     * The data file contains the mxlFlowInfo structure and is memory-mapped
     * by both readers and writers. It's the central coordination point for
     * flow metadata, including runtime head index and configuration.
     *
     * @param flowDirectory The flow's directory path
     * @return Path to the 'data' file within the flow directory
     */
    MXL_EXPORT
    std::filesystem::path makeFlowDataFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_DATA_FILE_NAME;
    }

    /**
     * @brief Construct the path to a flow's NMOS descriptor JSON file
     *
     * The flow_def.json file contains the original NMOS IS-04 flow definition
     * used to create the flow. This allows applications to retrieve the full
     * flow metadata (label, tags, media parameters, etc.) for display or routing.
     *
     * @param flowDirectory The flow's directory path
     * @return Path to the 'flow_def.json' file
     */
    MXL_EXPORT
    std::filesystem::path makeFlowDescriptorFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_DESCRIPTOR_FILE_NAME;
    }

    /**
     * @brief Construct the path to a flow's read access tracking file
     *
     * The access file is touched by readers to update lastReadTime. This allows
     * the DomainWatcher to detect when flows are actively being consumed, which
     * is used for garbage collection decisions.
     *
     * Note: On read-only filesystems, touching this file may fail, but MXL
     * continues to operate (just without read-time tracking).
     *
     * @param flowDirectory The flow's directory path
     * @return Path to the 'access' file
     */
    MXL_EXPORT
    std::filesystem::path makeFlowAccessFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_ACCESS_FILE_NAME;
    }

    /**
     * @brief Construct the path to the grains subdirectory (discrete flows only)
     *
     * Discrete flows (video/data) store individual grains in separate memory-mapped
     * files organized in a ring buffer. Each grain file contains a header and payload.
     *
     * @param flowDirectory The flow's directory path
     * @return Path to the 'grains/' subdirectory
     */
    MXL_EXPORT
    std::filesystem::path makeGrainDirectoryName(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / GRAIN_DIRECTORY_NAME;
    }

    /**
     * @brief Construct the path to a specific grain file (discrete flows only)
     *
     * Each grain in the ring buffer has its own memory-mapped file. The index
     * corresponds to the ring buffer position (not the absolute grain index).
     *
     * File naming: grain.0, grain.1, grain.2, ...
     *
     * @param grainDirectory The grains subdirectory path
     * @param index The ring buffer index (0 to grainCount-1)
     * @return Path to the specific grain file
     */
    MXL_EXPORT
    std::filesystem::path makeGrainDataFilePath(std::filesystem::path const& grainDirectory, unsigned int index)
    {
        return grainDirectory / fmt::format("{}.{}", GRAIN_DATA_FILE_NAME_STEM, index);
    }

    /**
     * @brief Construct the path to the channel data file (continuous flows only)
     *
     * Continuous flows (audio) store per-channel sample ring buffers in a single
     * memory-mapped file. The file is organized as strided buffers, one per channel.
     *
     * @param flowDirectory The flow's directory path
     * @return Path to the 'channels' file
     */
    MXL_EXPORT
    std::filesystem::path makeChannelDataFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / CHANNEL_DATA_FILE_NAME;
    }

    /**
     * @brief Construct the path to the domain-wide options file
     *
     * The options.json file contains domain-level configuration like history duration.
     * It's optional - if missing, defaults are used.
     *
     * @param domain The MXL domain path
     * @return Path to the 'options.json' file
     */
    MXL_EXPORT
    std::filesystem::path makeDomainOptionsFilePath(std::filesystem::path const& domain)
    {
        return domain / (DOMAIN_OPTIONS_FILE_NAME);
    }
}
