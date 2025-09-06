// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "FlowData.hpp"

namespace mxl::lib
{
    class FlowWriter
    {
    public:
        ///
        /// Accessor for the flow id;
        /// \return The flow id
        ///
        uuids::uuid const& getId() const;

        ///
        /// Accessor for the current mxlFlowInfo. A copy of the current structure is returned.
        /// The flow writer must first open the  flow before invoking this method.
        /// \return A copy of the mxlFlowInfo
        ///
        virtual mxlFlowInfo getFlowInfo() = 0;

        ///
        /// Accessor for the underlying flow data.
        /// The flow writer must first open the flow before invoking this method.
        ///
        virtual FlowData& getFlowData() = 0;

        /// Invoked when a flow is read. The writer will
        /// update the 'lastReadTime' field
        virtual void flowRead() = 0;

        virtual ~FlowWriter();

    protected:
        explicit FlowWriter(uuids::uuid&& flowId);
        explicit FlowWriter(uuids::uuid const& flowId);

    private:
        uuids::uuid _flowId;
    };
}
