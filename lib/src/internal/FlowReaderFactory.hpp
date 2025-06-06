#pragma once

#include <memory>
#include "DiscreteFlowReader.hpp"
#include "DiscreteFlowData.hpp"

namespace mxl::lib
{
    class FlowManager;

    class FlowReaderFactory
    {
        public:
            virtual std::unique_ptr<DiscreteFlowReader> createDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data) const = 0;

        protected:
            ~FlowReaderFactory() = default;
    };
}
