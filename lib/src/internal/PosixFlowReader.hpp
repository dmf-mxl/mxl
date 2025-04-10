#pragma once

#include "FlowManager.hpp"
#include "FlowReader.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <uuid.h>

namespace mxl::lib {

///
/// Implementation of a flow reader based on POSIX shared memory.
///
class PosixFlowReader : public FlowReader
{
public:
    /// Ctor.
    PosixFlowReader( FlowManager::ptr in_manager );

    ///
    /// \see FlowReader::open
    ///
    virtual bool open( const uuids::uuid &in_id ) override;

    ///
    /// Releases all the required shared memory structures associated with this flow.
    ///
    virtual void close() override;

    ///
    /// Accessor for the current FlowInfo. A copy of the current structure is returned.
    /// The reader must be properly attached to the flow before invoking this method.
    /// \return A copy of the FlowInfo
    ///
    virtual FlowInfo getFlowInfo() override;

    /// \see FlowReader::grainAvailable
    virtual void grainAvailable() override;

    /// \see FlowReader::getGrain
    virtual mxlStatus getGrain( uint64_t in_index, uint16_t in_timeoutMs, GrainInfo *out_grainInfo, GrainAccessor *out_accessor ) override;

    ///
    /// Dtor. Releases all resources.
    ///
    virtual ~PosixFlowReader();

private:
    FlowManager::ptr _manager;
    FlowData::ptr _flowData;
    int _accessFileFd = 1;
    std::mutex _grainMutex;
    std::condition_variable _grainCV;
};

} // namespace mxl::lib