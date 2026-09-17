// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <uuid.h>
#include "ContinuousFlowData.hpp"
#include "ContinuousFlowWriter.hpp"
#include "DiscreteFlowData.hpp"
#include "DiscreteFlowWriter.hpp"
#include "EventFlowData.hpp"
#include "EventFlowWriter.hpp"

namespace mxl::lib
{
    class FlowManager;

    class FlowWriterFactory
    {
    public:
        virtual std::unique_ptr<DiscreteFlowWriter> createDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<DiscreteFlowData>&& data) const = 0;
        virtual std::unique_ptr<ContinuousFlowWriter> createContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<ContinuousFlowData>&& data) const = 0;
        virtual std::unique_ptr<EventFlowWriter> createEventFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<EventFlowData>&& data) const = 0;

    protected:
        ~FlowWriterFactory() = default;
    };
}
