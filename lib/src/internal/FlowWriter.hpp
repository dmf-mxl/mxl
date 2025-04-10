#pragma once

#include <cstdint>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <uuid.h>

namespace mxl::lib {

class FlowWriter
{
public:
    virtual bool open( const uuids::uuid &in_id ) = 0;

    virtual void close() = 0;

    ///
    /// Accessor for the flow id;
    /// \return The flow id
    ///
    uuids::uuid getId() const { return _flowId; }

    ///
    /// Accessor for the current FlowInfo. A copy of the current structure is returned.
    /// The flow writer must first open the  flow before invoking this method.
    /// \return A copy of the FlowInfo
    ///
    virtual FlowInfo getFlowInfo() = 0;

    ///
    /// Open a grain for mutation.  The flow writer will remember which index is currently opened.
    ///
    /// \param in_index The grain index.  will return MXL_ERR_OUT_OF_RANGE if the index is out of range.
    /// \param inout_grainInfo The grain info structure.  The sliceCount field is used to determine how many slices
    /// are in the grain.
    /// \param out_payload The memory buffer where the grain payload is stored.
    /// \return The result code. \see mxlStatus
    ///
    virtual mxlStatus openGrain( uint64_t in_index, GrainInfo *inout_grainInfo, uint8_t **out_payload ) = 0;

    virtual mxlStatus cancel() = 0;

    virtual mxlStatus commit( const GrainInfo *in_grainInfo ) = 0;

    /// Increments the current slice index in case of multi-slice grains.
    /// This should be used when writing multi-slice grains (GrainInfo::sliceCount > 1).
    /// \return \see mxlStatus
    virtual mxlStatus incrementSlice( GrainInfo *out_grainInfo ) = 0;

    virtual ~FlowWriter() = default;

    /// Invoked when a flow is read. The writer will
    /// update the 'lastReadTime' field
    virtual void flowRead() = 0;

protected:
    uuids::uuid _flowId;
};

} // namespace mxl::lib