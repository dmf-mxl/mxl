#include "FlowIoFactory.hpp"
#include "FlowWriterFactory.hpp"

namespace mxl::lib
{
    namespace
    {
        template<typename To, typename From>
        std::unique_ptr<To> dynamic_pointer_cast(std::unique_ptr<From>&& source) noexcept
        {
            auto const p = dynamic_cast<To*>(source.get());
            if (p != nullptr)
            {
                source.release();
            }
            return std::unique_ptr<To>{p};
        }
    }

    FlowIoFactory::FlowIoFactory() = default;

    FlowIoFactory::~FlowIoFactory() = default;

    std::unique_ptr<FlowReader> FlowIoFactory::createFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<FlowData>&& data)
    {
        if (auto discreteData = dynamic_pointer_cast<DiscreteFlowData>(std::move(data)); discreteData)
        {
            return this->createDiscreteFlowReader(manager, flowId, std::move(discreteData));
        }
        throw std::runtime_error("Could not create reader, because flow type is not supported.");
    }

    std::unique_ptr<FlowWriter> FlowIoFactory::createFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<FlowData>&& data)
    {
        if (auto discreteData = dynamic_pointer_cast<DiscreteFlowData>(std::move(data)); discreteData)
        {
            return this->createDiscreteFlowWriter(manager, flowId, std::move(discreteData));
        }
        throw std::runtime_error("Could not create writer, because flow type is not supported.");
    }
}

