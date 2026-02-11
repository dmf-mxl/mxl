// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowWriter.cpp
 * @brief Base class implementation for MXL flow writers
 *
 * FlowWriter is the abstract base class for all flow writer implementations in MXL.
 * It provides the common interface and storage for flow writers that produce media
 * data into shared-memory ring buffers.
 *
 * Inheritance hierarchy:
 *   FlowWriter (abstract base)
 *     ├── DiscreteFlowWriter (abstract, adds grain-based operations)
 *     │   └── PosixDiscreteFlowWriter (concrete POSIX implementation)
 *     └── ContinuousFlowWriter (abstract, adds sample-based operations)
 *         └── PosixContinuousFlowWriter (concrete POSIX implementation)
 *
 * The base class is kept minimal, providing only the flow ID management that's
 * common to all writer types. The actual write operations are defined in derived
 * classes based on the flow format (discrete vs continuous).
 *
 * Thread safety:
 * - FlowWriter instances are not thread-safe
 * - Each writer should be used from a single thread at a time
 * - Multiple writers can write to the same flow (shared lock semantics)
 */

#include "mxl-internal/FlowWriter.hpp"
#include <utility>

namespace mxl::lib
{
    /**
     * @brief Construct a flow writer with move semantics for the flow ID
     *
     * @param flowId The UUID identifying the flow (moved)
     */
    FlowWriter::FlowWriter(uuids::uuid&& flowId)
        : _flowId{std::move(flowId)}
    {}

    /**
     * @brief Construct a flow writer by copying the flow ID
     *
     * @param flowId The UUID identifying the flow (copied)
     */
    FlowWriter::FlowWriter(uuids::uuid const& flowId)
        : _flowId{flowId}
    {}

    /**
     * @brief Virtual destructor for proper cleanup in derived classes
     *
     * The destructor is virtual to ensure derived class destructors are called
     * properly when deleting through a base class pointer.
     */
    FlowWriter::FlowWriter::~FlowWriter() = default;

    /**
     * @brief Get the UUID of the flow this writer is writing to
     *
     * @return Const reference to the flow's UUID
     */
    uuids::uuid const& FlowWriter::getId() const
    {
        return _flowId;
    }
}
