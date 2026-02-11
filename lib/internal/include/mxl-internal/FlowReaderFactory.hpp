// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowReaderFactory.hpp
 * @brief Abstract factory interface for creating FlowReaders
 *
 * Part of the Abstract Factory pattern for platform-specific FlowReader construction.
 * Concrete implementation: PosixFlowIoFactory (inherits from both this and FlowWriterFactory).
 */

#pragma once

#include <memory>
#include "ContinuousFlowData.hpp"
#include "ContinuousFlowReader.hpp"
#include "DiscreteFlowData.hpp"
#include "DiscreteFlowReader.hpp"

namespace mxl::lib
{
    class FlowManager;

    /**
     * Abstract factory interface for creating FlowReaders.
     * Concrete implementation: PosixFlowIoFactory.
     */
    class FlowReaderFactory
    {
    public:
        /** Create a reader for discrete flows (VIDEO/DATA). */
        virtual std::unique_ptr<DiscreteFlowReader> createDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<DiscreteFlowData>&& data) const = 0;

        /** Create a reader for continuous flows (AUDIO). */
        virtual std::unique_ptr<ContinuousFlowReader> createContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<ContinuousFlowData>&& data) const = 0;

    protected:
        /** Protected destructor (interface only, not directly instantiated). */
        ~FlowReaderFactory() = default;
    };
}
