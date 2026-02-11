// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowReader.cpp
 * @brief Base class implementation for MXL flow readers
 *
 * FlowReader is the abstract base class for all flow reader implementations in MXL.
 * It provides the common interface and storage for flow readers that consume media
 * data from shared-memory ring buffers.
 *
 * Inheritance hierarchy:
 *   FlowReader (abstract base)
 *     ├── DiscreteFlowReader (abstract, adds grain-based operations)
 *     │   └── PosixDiscreteFlowReader (concrete POSIX implementation)
 *     └── ContinuousFlowReader (abstract, adds sample-based operations)
 *         └── PosixContinuousFlowReader (concrete POSIX implementation)
 *
 * The base class provides:
 * - Flow ID for identifying which flow is being read
 * - Domain path for locating the flow's files
 * - Common validation interface (isFlowValid)
 *
 * Readers use read-only memory mappings (PROT_READ) to enable zero-copy access
 * to shared data without the ability to corrupt the flow.
 *
 * Thread safety:
 * - FlowReader instances are const-correct and thread-safe for concurrent reads
 * - Multiple readers can read the same flow simultaneously
 * - Readers synchronize with writers using futex-based wait operations
 */

#include "mxl-internal/FlowReader.hpp"
#include <utility>

namespace mxl::lib
{
    /**
     * @brief Construct a flow reader with move semantics for the flow ID
     *
     * @param flowId The UUID identifying the flow (moved)
     * @param domain The MXL domain path where the flow is located
     */
    FlowReader::FlowReader(uuids::uuid&& flowId, std::filesystem::path const& domain)
        : _flowId{std::move(flowId)}
        , _domain{domain}
    {}

    /**
     * @brief Construct a flow reader by copying the flow ID
     *
     * @param flowId The UUID identifying the flow (copied)
     * @param domain The MXL domain path where the flow is located
     */
    FlowReader::FlowReader(uuids::uuid const& flowId, std::filesystem::path const& domain)
        : _flowId{flowId}
        , _domain{domain}
    {}

    /**
     * @brief Virtual destructor for proper cleanup in derived classes
     *
     * The destructor is virtual to ensure derived class destructors are called
     * properly when deleting through a base class pointer.
     */
    FlowReader::FlowReader::~FlowReader() = default;

    /**
     * @brief Get the UUID of the flow this reader is reading from
     *
     * @return Const reference to the flow's UUID
     */
    uuids::uuid const& FlowReader::getId() const
    {
        return _flowId;
    }

    /**
     * @brief Get the domain path where this flow is located
     *
     * The domain path is needed to construct file paths and verify flow validity
     * by checking that the flow's data file still exists and has the correct inode.
     *
     * @return Const reference to the domain filesystem path
     */
    std::filesystem::path const& FlowReader::getDomain() const
    {
        return _domain;
    }
}
