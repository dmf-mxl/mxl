// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/DomainWatcher.hpp"
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
         * Check if this is the only writer for this flow.
         */
        virtual bool isExclusive() const = 0;

        /**
         * Try to turn this into a exclusive writer.
         * \return true if the operation was successful and this is the only writer, false if the operation was not successful because there are other
         * active writers for this flow.
         */
        virtual bool makeExclusive() = 0;

        /** Destructor. */
        virtual ~FlowWriter();

    protected:
        explicit FlowWriter(uuids::uuid&& flowId);
        explicit FlowWriter(uuids::uuid const& flowId);

    private:
        uuids::uuid _flowId;
    };
}
