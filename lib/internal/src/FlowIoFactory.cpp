// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowIoFactory.cpp
 * @brief Abstract factory for creating flow readers and writers
 *
 * The FlowIoFactory provides a polymorphic interface for creating readers and writers
 * without the caller needing to know the concrete implementation type. This allows
 * different backends (POSIX, potentially future GPU/RDMA implementations) to be
 * plugged in at runtime.
 *
 * Design pattern: Abstract Factory
 * - FlowIoFactory: Abstract factory interface
 * - PosixFlowIoFactory: Concrete factory for POSIX shared memory implementation
 * - Future factories could implement GPU memory, RDMA, etc.
 *
 * The factory handles the type dispatch between discrete and continuous flows
 * by using dynamic_pointer_cast to determine the concrete FlowData type, then
 * delegating to the appropriate specialized create method.
 *
 * Flow type hierarchy:
 *   FlowData (abstract)
 *     ├── DiscreteFlowData (video/data grains)
 *     └── ContinuousFlowData (audio samples)
 *
 * This architecture enables zero-copy access to shared memory while maintaining
 * type safety and clean separation of concerns.
 */

#include "mxl-internal/FlowIoFactory.hpp"
#include "mxl-internal/FlowWriterFactory.hpp"
#include "DynamicPointerCast.hpp"

namespace mxl::lib
{
    /**
     * @brief Default constructor for the factory base class
     */
    FlowIoFactory::FlowIoFactory() = default;

    /**
     * @brief Virtual destructor for proper cleanup of derived factories
     */
    FlowIoFactory::~FlowIoFactory() = default;

    /**
     * @brief Create a reader for a flow based on its runtime type
     *
     * This method performs runtime type dispatch to determine whether the flow
     * is discrete or continuous, then delegates to the appropriate specialized
     * creation method. The type is determined by dynamic_cast on the FlowData.
     *
     * @param manager Reference to the flow manager for domain context
     * @param flowId UUID of the flow to read
     * @param data Flow metadata (ownership transferred, will be moved into reader)
     * @return Polymorphic pointer to the created reader (discrete or continuous)
     *
     * @throws std::runtime_error if the flow type is neither discrete nor continuous
     *
     * @note The data parameter is moved from and will be empty after this call
     */
    std::unique_ptr<FlowReader> FlowIoFactory::createFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<FlowData>&& data)
    {
        // Try to cast to DiscreteFlowData (video/data)
        if (auto discreteData = dynamic_pointer_cast<DiscreteFlowData>(std::move(data)); discreteData)
        {
            return this->createDiscreteFlowReader(manager, flowId, std::move(discreteData));
        }

        // Try to cast to ContinuousFlowData (audio)
        if (auto continuousData = dynamic_pointer_cast<ContinuousFlowData>(std::move(data)); continuousData)
        {
            return this->createContinuousFlowReader(manager, flowId, std::move(continuousData));
        }

        // Unknown flow type (should never happen with valid FlowData)
        throw std::runtime_error("Could not create reader, because flow type is not supported.");
    }

    /**
     * @brief Create a writer for a flow based on its runtime type
     *
     * This method performs runtime type dispatch to determine whether the flow
     * is discrete or continuous, then delegates to the appropriate specialized
     * creation method. The type is determined by dynamic_cast on the FlowData.
     *
     * @param manager Reference to the flow manager for domain context
     * @param flowId UUID of the flow to write
     * @param data Flow metadata (ownership transferred, will be moved into writer)
     * @return Polymorphic pointer to the created writer (discrete or continuous)
     *
     * @throws std::runtime_error if the flow type is neither discrete nor continuous
     *
     * @note The data parameter is moved from and will be empty after this call
     */
    std::unique_ptr<FlowWriter> FlowIoFactory::createFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<FlowData>&& data)
    {
        // Try to cast to DiscreteFlowData (video/data)
        if (auto discreteData = dynamic_pointer_cast<DiscreteFlowData>(std::move(data)); discreteData)
        {
            return this->createDiscreteFlowWriter(manager, flowId, std::move(discreteData));
        }

        // Try to cast to ContinuousFlowData (audio)
        if (auto continuousData = dynamic_pointer_cast<ContinuousFlowData>(std::move(data)); continuousData)
        {
            return this->createContinuousFlowWriter(manager, flowId, std::move(continuousData));
        }

        // Unknown flow type (should never happen with valid FlowData)
        throw std::runtime_error("Could not create writer, because flow type is not supported.");
    }
}
