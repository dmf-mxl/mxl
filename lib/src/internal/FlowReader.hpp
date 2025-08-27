// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>

namespace mxl::lib
{
    class FlowReader
    {
    public:
        ///
        /// Accessor for the flow id;
        /// \return The flow id
        ///
        uuids::uuid const& getId() const;

        ///
        /// Accessor for the current mxlFlowInfo. A copy of the current structure is returned.
        /// The reader must be properly attached to the flow before invoking this method.
        /// \return A copy of the mxlFlowInfo
        ///
        virtual mxlFlowInfo getFlowInfo() = 0;

        ///
        /// Dtor.
        ///
        virtual ~FlowReader();

    protected:
        explicit FlowReader(uuids::uuid&& flowId);
        explicit FlowReader(uuids::uuid const& flowId);

    private:
        uuids::uuid _flowId;
    };

} // namespace mxl::lib