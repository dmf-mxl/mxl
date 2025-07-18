// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

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
