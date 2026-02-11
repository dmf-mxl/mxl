// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowWriterFactory.hpp
 * @brief Abstract factory interface for creating FlowWriters
 *
 * Part of the Abstract Factory pattern for platform-specific FlowWriter construction.
 * Concrete implementation: PosixFlowIoFactory (inherits from both this and FlowReaderFactory).
 */

#pragma once

#include <memory>
#include <uuid.h>
#include "ContinuousFlowData.hpp"
#include "ContinuousFlowWriter.hpp"
#include "DiscreteFlowData.hpp"
#include "DiscreteFlowWriter.hpp"

namespace mxl::lib
{
    class FlowManager;

    /**
     * Abstract factory interface for creating FlowWriters.
     * Concrete implementation: PosixFlowIoFactory.
     */
    class FlowWriterFactory
    {
    public:
        /** Create a writer for discrete flows (VIDEO/DATA). */
        virtual std::unique_ptr<DiscreteFlowWriter> createDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<DiscreteFlowData>&& data) const = 0;

        /** Create a writer for continuous flows (AUDIO). */
        virtual std::unique_ptr<ContinuousFlowWriter> createContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<ContinuousFlowData>&& data) const = 0;

    protected:
        /** Protected destructor (interface only, not directly instantiated). */
        ~FlowWriterFactory() = default;
    };
}
