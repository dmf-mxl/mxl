#pragma once

#include <cstdint>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <string>
#include <uuid.h>

namespace mxl::lib {

class FlowReader
{
protected:
    std::string _options;
    uuids::uuid _flowId;

public:
    ///
    /// Opens all the required shared memory structures associated with this flow.
    ///
    /// \param in_id The flow id.  Must be valid and refer to an existing flow.
    /// \return true on success
    ///
    virtual bool open( const uuids::uuid &in_id ) = 0;

    ///
    /// Releases all the required shared memory structures associated with this flow.
    ///
    virtual void close() = 0;

    ///
    /// Accessor for the flow id;
    /// \return The flow id
    ///
    uuids::uuid getId() const { return _flowId; }

    ///
    /// Accessor for the current FlowInfo. A copy of the current structure is returned.
    /// The reader must be properly attached to the flow before invoking this method.
    /// \return A copy of the FlowInfo
    ///
    virtual FlowInfo getFlowInfo() = 0;

    /// Invoked when a new grain is available.
    /// This will signal readers waiting for the next grain.
    virtual void grainAvailable() = 0;

    ///
    /// Accessor for a specific grain at a specific index.
    /// The index must be >= FlowInfo.tailIndex.
    ///
    /// \param in_index The grain index.
    /// \param in_timeoutMs How long to wait for the grain if in_index is > FlowInfo.headIndex
    /// \param out_grainInfo A valid pointer to GrainInfo that will be copied to
    /// \param out_accessor A grain accessor that will be filled if grain access is successful
    ///
    /// \return A status code describing the outcome of the call.
    ///
    virtual mxlStatus getGrain( uint64_t in_index, uint16_t in_timeoutMs, GrainInfo *out_grainInfo, GrainAccessor *out_accesor ) = 0;

    ///
    /// Dtor.
    ///
    virtual ~FlowReader() = default;
};

} // namespace mxl::lib