// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/FlowData.hpp"

namespace mxl::lib
{
    class MXL_EXPORT FlowWriter
    {
    public:
        /**
         * Accessor for the flow id;
         * \return The flow id
         */
        [[nodiscard]]
        uuids::uuid const& getId() const;

        /**
         * Accessor for the underlying flow data.
         * The flow writer must first open the flow before invoking this method.
         */
        [[nodiscard]]
        virtual FlowData const& getFlowData() const = 0;

        /**
         * Accessor for the current mxlFlowInfo. A copy of the current structure is returned.
         * The flow writer must first open the  flow before invoking this method.
         * \return A copy of the mxlFlowInfo
         */
        [[nodiscard]]
        virtual mxlFlowInfo getFlowInfo() const = 0;

        /**
         * Accessor for the immutable metadata that describes the currently opened flow.
         * The reader must be properly attached to the flow before invoking this method.
         * \return A copy of the mxlFlowConfigInfo of the currently opened flow.
         */
        [[nodiscard]]
        virtual mxlFlowConfigInfo getFlowConfigInfo() const = 0;

        /**
         * Accessor for the current metadata that describes the current runtime state of
         * the currently opened flow. The reader must be properly attached to the flow before
         * invoking this method.
         * \return A copy of the current mxlFlowRuntimeInfo of the currently opened flow.
         */
        [[nodiscard]]
        virtual mxlFlowRuntimeInfo getFlowRuntimeInfo() const = 0;

        /**
         * Invoked when a flow is read. The writer will
         * update the 'lastReadTime' field
         */
        virtual void flowRead() = 0;

        /** Destructor. */
        virtual ~FlowWriter();

    protected:
        explicit FlowWriter(uuids::uuid&& flowId);
        explicit FlowWriter(uuids::uuid const& flowId);

    private:
        uuids::uuid _flowId;
    };
}
