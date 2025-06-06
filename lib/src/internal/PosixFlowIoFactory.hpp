#pragma once

#include "FlowIoFactory.hpp"

namespace mxl::lib
{
    struct PosixFlowIoFactory
        : FlowIoFactory
    {
        /** \see FlowReaderFactory::createDiscreteFlowReader() */
        virtual std::unique_ptr<DiscreteFlowReader> createDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data) const override;
        /** \see FlowWriterFactory::createDiscreteFlowWriter() */
        virtual std::unique_ptr<DiscreteFlowWriter> createDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data) const override;

        ~PosixFlowIoFactory();
    };
}
