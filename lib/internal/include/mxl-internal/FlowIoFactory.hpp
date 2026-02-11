// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowIoFactory.hpp
 * @brief Factory for creating FlowReaders and FlowWriters (polymorphic dispatch)
 *
 * FlowIoFactory combines FlowReaderFactory and FlowWriterFactory into a single
 * factory interface used by Instance to create reader/writer objects.
 *
 * DESIGN PATTERN: Abstract Factory
 * - FlowIoFactory is the abstract interface
 * - PosixFlowIoFactory is the concrete implementation for POSIX systems
 * - Allows platform-specific implementations (future: Windows, embedded, etc.)
 *
 * POLYMORPHIC DISPATCH:
 * - createFlowReader()/createFlowWriter() examine FlowData type (discrete vs. continuous)
 * - Return appropriate concrete reader/writer (DiscreteFlowReader vs. ContinuousFlowReader)
 * - Application code works with abstract FlowReader/FlowWriter interfaces
 *
 * WHY FACTORY PATTERN?
 * - Decouples Instance from concrete reader/writer implementations
 * - Enables platform-specific optimizations (e.g., io_uring on Linux)
 * - Simplifies testing (mock factory for unit tests)
 * - Supports runtime selection of I/O strategy
 */

#pragma once

#include "FlowReaderFactory.hpp"
#include "FlowWriterFactory.hpp"

namespace mxl::lib
{
    /**
     * Combined factory for creating both readers and writers.
     * Abstract base class; concrete implementation: PosixFlowIoFactory.
     */
    class MXL_EXPORT FlowIoFactory
        : public FlowReaderFactory
        , public FlowWriterFactory
    {
    public:
        /**
         * Create a FlowReader for the specified flow.
         * Examines FlowData type and returns DiscreteFlowReader or ContinuousFlowReader.
         * @param manager FlowManager reference (for flow metadata access)
         * @param flowId Flow UUID
         * @param data FlowData with opened flow (ownership transferred)
         * @return Polymorphic FlowReader pointer (DiscreteFlowReader or ContinuousFlowReader)
         */
        std::unique_ptr<FlowReader> createFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<FlowData>&& data);

        /**
         * Create a FlowWriter for the specified flow.
         * Examines FlowData type and returns DiscreteFlowWriter or ContinuousFlowWriter.
         * @param manager FlowManager reference (for flow metadata access)
         * @param flowId Flow UUID
         * @param data FlowData with opened/created flow (ownership transferred)
         * @return Polymorphic FlowWriter pointer (DiscreteFlowWriter or ContinuousFlowWriter)
         */
        std::unique_ptr<FlowWriter> createFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<FlowData>&& data);

        /** Virtual destructor for polymorphic base class. */
        virtual ~FlowIoFactory();

    protected:
        /** Protected constructor (factory is abstract, only derived classes instantiated). */
        FlowIoFactory();
    };
}
