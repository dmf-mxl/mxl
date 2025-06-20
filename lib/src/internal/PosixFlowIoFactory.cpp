#include "PosixFlowIoFactory.hpp"
#include "PosixContinuousFlowReader.hpp"
#include "PosixContinuousFlowWriter.hpp"
#include "PosixDiscreteFlowReader.hpp"
#include "PosixDiscreteFlowWriter.hpp"

namespace mxl::lib
{
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
        return std::make_unique<PosixDiscreteFlowWriter>(manager, flowId, std::move(data));
    }

    std::unique_ptr<ContinuousFlowWriter> PosixFlowIoFactory::createContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<ContinuousFlowData>&& data) const
    {
        return std::make_unique<PosixContinuousFlowWriter>(manager, flowId, std::move(data));
    }
}
