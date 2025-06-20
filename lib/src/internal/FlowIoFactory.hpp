#pragma once

#include "FlowReaderFactory.hpp"
#include "FlowWriterFactory.hpp"

namespace mxl::lib
{
    class FlowIoFactory
        : public FlowReaderFactory
        , public FlowWriterFactory
    {
    public:
        std::unique_ptr<FlowReader> createFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<FlowData>&& data);
        std::unique_ptr<FlowWriter> createFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<FlowData>&& data);

        virtual ~FlowIoFactory();

    protected:
        FlowIoFactory();
    };
}
