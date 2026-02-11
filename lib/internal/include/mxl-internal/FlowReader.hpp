// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowReader.hpp
 * @brief Abstract base class for reading media from MXL flows
 *
 * FlowReader is the interface for consuming media from shared memory flows.
 * It's specialized by:
 * - DiscreteFlowReader (for VIDEO/DATA): reads individual grains
 * - ContinuousFlowReader (for AUDIO): reads sample buffers
 *
 * ARCHITECTURE:
 *
 *   FlowReader (this file - abstract base)
 *     - Provides common metadata access (flowInfo, configInfo, runtimeInfo)
 *     - Tracks flow ID and domain
 *     - Validates flow existence and staleness
 *     |
 *     +-- DiscreteFlowReader
 *     |     - Grain API: getGrain(), releaseGrain(), waitForGrain()
 *     |     - Maps grain files on demand
 *     |     - Tracks grain ring buffer position
 *     |
 *     +-- ContinuousFlowReader
 *           - Samples API: readSamples(), waitForSamples()
 *           - Maps "channels" file (sample ring buffer)
 *           - Tracks per-channel read position
 *
 * KEY RESPONSIBILITIES:
 * - Open and validate flow (check inode for staleness)
 * - Provide zero-copy access to media data
 * - Wait for new data using futex (efficient cross-process wait)
 * - Detect flow recreation (inode change)
 * - Return copies of metadata structures (safe to cache)
 *
 * USAGE PATTERN:
 * 1. Instance::getFlowReader(flowId) creates and opens reader
 * 2. Reader maps flow "data" file, validates inode
 * 3. Application calls getGrain() or readSamples() to consume media
 * 4. Reader waits on syncCounter futex if no new data available
 * 5. Application calls Instance::releaseReader() when done
 *
 * Thread-safety:
 * - Each thread must have its own FlowReader instance
 * - Multiple readers can access the same flow concurrently
 * - Read-only mappings (PROT_READ) prevent accidental corruption
 *
 * Stale flow detection:
 * - FlowState::inode is compared against current file's inode
 * - If different, flow was deleted and recreated (must remap)
 * - isFlowValid() performs this check before each access
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/FlowData.hpp"

namespace mxl::lib
{
    /**
     * Abstract base class for flow readers (VIDEO/AUDIO/DATA consumers).
     *
     * Concrete implementations:
     * - DiscreteFlowReader (grains for VIDEO/DATA)
     * - ContinuousFlowReader (samples for AUDIO)
     *
     * Polymorphic (has virtual destructor and pure virtual methods).
     */
    class MXL_EXPORT FlowReader
    {
    public:
        /**
         * Get the UUID of the flow being read.
         * @return Flow UUID (immutable after construction)
         */
        [[nodiscard]]
        uuids::uuid const& getId() const;

        /**
         * Get the domain path where the flow resides.
         * @return Domain path (e.g., "/dev/shm/mxl" or "/tmp/mxl-domain")
         */
        [[nodiscard]]
        std::filesystem::path const& getDomain() const;

        /**
         * Get the underlying FlowData (shared memory mapping).
         *
         * Provides access to Flow structure in "data" file.
         * Reader must have successfully opened the flow first.
         *
         * @return Const reference to FlowData
         * @throws May throw if flow not opened (implementation-defined)
         */
        [[nodiscard]]
        virtual FlowData const& getFlowData() const = 0;

        /**
         * Get a copy of the current flow metadata.
         *
         * Returns: Ring buffer params, format, dimensions, grain rate, etc.
         * Safe to cache (returns copy, not reference to shared memory).
         *
         * Reader must have successfully opened the flow first.
         *
         * @return Copy of mxlFlowInfo structure
         * @throws May throw if flow not opened (implementation-defined)
         */
        [[nodiscard]]
        virtual mxlFlowInfo getFlowInfo() const = 0;

        /**
         * Get a copy of the immutable flow configuration metadata.
         *
         * Returns: Flow creation parameters, buffer sizes, format details.
         * This is the subset of flowInfo that never changes after creation.
         *
         * Reader must have successfully opened the flow first.
         *
         * @return Copy of mxlFlowConfigInfo structure
         * @throws May throw if flow not opened (implementation-defined)
         */
        [[nodiscard]]
        virtual mxlFlowConfigInfo getFlowConfigInfo() const = 0;

        /**
         * Get a copy of the current runtime state metadata.
         *
         * Returns: Current grain count, sample positions, writer activity, etc.
         * This is the subset of flowInfo that changes during operation.
         * Snapshot may be stale immediately after return (volatile data).
         *
         * Reader must have successfully opened the flow first.
         *
         * @return Copy of mxlFlowRuntimeInfo structure
         * @throws May throw if flow not opened (implementation-defined)
         */
        [[nodiscard]]
        virtual mxlFlowRuntimeInfo getFlowRuntimeInfo() const = 0;

        /**
         * Virtual destructor for polymorphic base class.
         * Derived classes close mappings, release resources.
         */
        virtual ~FlowReader();

    protected:
        /**
         * Validate that the flow still exists and hasn't been recreated.
         *
         * Checks:
         * 1. "data" file exists and is accessible
         * 2. Current file inode matches FlowState::inode
         *
         * If validation fails (inode mismatch), the flow was deleted and recreated.
         * Reader should remap or return error to application.
         *
         * @return true if flow is valid, false if stale or missing
         */
        [[nodiscard]]
        virtual bool isFlowValid() const = 0;

    protected:
        /**
         * Protected constructor: move flow ID.
         * Used by derived classes during construction.
         * @param flowId Flow UUID (moved)
         * @param domain Domain path where flow resides
         */
        explicit FlowReader(uuids::uuid&& flowId, std::filesystem::path const& domain);

        /**
         * Protected constructor: copy flow ID.
         * Used by derived classes during construction.
         * @param flowId Flow UUID (copied)
         * @param domain Domain path where flow resides
         */
        explicit FlowReader(uuids::uuid const& flowId, std::filesystem::path const& domain);

    private:
        /**
         * UUID of the flow being read.
         * Used to construct flow directory path (${domain}/${flowId}.mxl-flow).
         * Immutable after construction.
         */
        uuids::uuid _flowId;

        /**
         * Domain path where the flow resides.
         * Typically a tmpfs mount (e.g., /dev/shm/mxl) for best performance.
         * Immutable after construction.
         */
        std::filesystem::path _domain;
    };

} // namespace mxl::lib
