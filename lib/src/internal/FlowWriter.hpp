#pragma once

#include <cstdint>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>

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
        /// Accessor for the current FlowInfo. A copy of the current structure is returned.
        /// The flow writer must first open the  flow before invoking this method.
        /// \return A copy of the FlowInfo
        ///
        virtual FlowInfo getFlowInfo() = 0;

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
