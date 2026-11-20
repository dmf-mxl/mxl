// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowIoFactory.hpp"
#include "mxl-internal/FlowWriterFactory.hpp"
#include "DynamicPointerCast.hpp"

namespace mxl::lib
{
    FlowIoFactory::FlowIoFactory() = default;

    FlowIoFactory::~FlowIoFactory() = default;

    std::unique_ptr<FlowReader> FlowIoFactory::createFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<FlowData>&& data)
    {
        if (auto discreteData = dynamic_pointer_cast<DiscreteFlowData>(std::move(data)); discreteData)
        {
            return this->createDiscreteFlowReader(manager, flowId, std::move(discreteData));
        }
        if (auto continuousData = dynamic_pointer_cast<ContinuousFlowData>(std::move(data)); continuousData)
        {
            return this->createContinuousFlowReader(manager, flowId, std::move(continuousData));
        }
        if (auto eventData = dynamic_pointer_cast<EventFlowData>(std::move(data)); eventData)
        {
            return this->createEventFlowReader(manager, flowId, std::move(eventData));
        }
        throw std::runtime_error("Could not create reader, because flow type is not supported.");
    }

    std::unique_ptr<FlowWriter> FlowIoFactory::createFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<FlowData>&& data)
    {
        if (auto discreteData = dynamic_pointer_cast<DiscreteFlowData>(std::move(data)); discreteData)
        {
            return this->createDiscreteFlowWriter(manager, flowId, std::move(discreteData));
        }
        if (auto continuousData = dynamic_pointer_cast<ContinuousFlowData>(std::move(data)); continuousData)
        {
            return this->createContinuousFlowWriter(manager, flowId, std::move(continuousData));
        }
        if (auto eventData = dynamic_pointer_cast<EventFlowData>(std::move(data)); eventData)
        {
            return this->createEventFlowWriter(manager, flowId, std::move(eventData));
        }
        throw std::runtime_error("Could not create writer, because flow type is not supported.");
    }
}
