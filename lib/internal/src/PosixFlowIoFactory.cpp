// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/PosixFlowIoFactory.hpp"
#include "PosixContinuousFlowReader.hpp"
#include "PosixContinuousFlowWriter.hpp"
#include "PosixDiscreteFlowReader.hpp"
#include "PosixDiscreteFlowWriter.hpp"
#include "PosixEventFlowReader.hpp"
#include "PosixEventFlowWriter.hpp"

namespace mxl::lib
{
    PosixFlowIoFactory::PosixFlowIoFactory(DomainWatcher::ptr watcher)
        : _watcher{std::move(watcher)}
    {}

    PosixFlowIoFactory::~PosixFlowIoFactory() = default;

    std::unique_ptr<DiscreteFlowReader> PosixFlowIoFactory::createDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<DiscreteFlowData>&& data) const
    {
        return std::make_unique<PosixDiscreteFlowReader>(manager, flowId, std::move(data));
    }

    std::unique_ptr<ContinuousFlowReader> PosixFlowIoFactory::createContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<ContinuousFlowData>&& data) const
    {
        return std::make_unique<PosixContinuousFlowReader>(manager, flowId, std::move(data));
    }

    std::unique_ptr<DiscreteFlowWriter> PosixFlowIoFactory::createDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<DiscreteFlowData>&& data) const
    {
        return std::make_unique<PosixDiscreteFlowWriter>(manager, flowId, std::move(data), _watcher);
    }

    std::unique_ptr<ContinuousFlowWriter> PosixFlowIoFactory::createContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<ContinuousFlowData>&& data) const
    {
        return std::make_unique<PosixContinuousFlowWriter>(manager, flowId, std::move(data));
    }

    std::unique_ptr<EventFlowReader> PosixFlowIoFactory::createEventFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<EventFlowData>&& data) const
    {
        return std::make_unique<PosixEventFlowReader>(manager, flowId, std::move(data));
    }

    std::unique_ptr<EventFlowWriter> PosixFlowIoFactory::createEventFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<EventFlowData>&& data) const
    {
        return std::make_unique<PosixEventFlowWriter>(manager, flowId, std::move(data), _watcher);
    }
}
