#pragma once

#include <memory>
#include <uuid.h>
#include "DiscreteFlowWriter.hpp"
#include "DiscreteFlowData.hpp"

namespace mxl::lib
{
    class FlowManager;

    class FlowWriterFactory
    {
        public:
            virtual std::unique_ptr<DiscreteFlowWriter> createDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data) const = 0;

        protected:
            ~FlowWriterFactory() = default;
    };
}
