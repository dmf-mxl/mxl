#include "PosixFlowWriter.hpp"

#include "Flow.hpp"
#include "FlowManager.hpp"
#include "SharedMem.hpp"

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <stdexcept>
#include <sys/stat.h>
#include <uuid.h>

namespace mxl::lib {

PosixFlowWriter::PosixFlowWriter( FlowManager::ptr in_manager ) : _manager{ in_manager } {}

bool
PosixFlowWriter::open( const uuids::uuid &in_id )
{
    _flowData = _manager->openFlow( in_id, AccessMode::READ_WRITE );
    if ( _flowData )
    {
        _flowId = in_id;
        return true;
    }
    else
    {
        return false;
    }
}

void
PosixFlowWriter::close()
{
    if ( _flowData && _flowData->flow )
    {
        _manager->closeFlow( _flowData );
        _flowData.reset();
        _flowId = uuids::uuid(); // reset to nil
        _currentIndex = MXL_UNDEFINED_OFFSET;
    }
}

FlowInfo
PosixFlowWriter::getFlowInfo()
{
    if ( _flowData )
    {
        return _flowData->flow->get()->info;
    }
    else
    {
        throw std::runtime_error( "No open flow." );
    }
}

mxlStatus
PosixFlowWriter::openGrain( uint64_t in_index, GrainInfo *inout_grainInfo, uint8_t **out_payload )
{
    if ( _flowData )
    {
        uint32_t offset = in_index % _flowData->flow->get()->info.grainCount;

        auto grain = _flowData->grains.at( offset )->get();
        grain->info.sliceCount = inout_grainInfo->sliceCount;
        grain->info.validSliceCount = 0;
        *inout_grainInfo = grain->info;
        *out_payload = reinterpret_cast<uint8_t *>( grain ) + MXL_GRAIN_PAYLOAD_OFFSET;
        _currentIndex = in_index;
        return MXL_STATUS_OK;
    }
    else
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
PosixFlowWriter::cancel()
{
    _currentIndex = MXL_UNDEFINED_OFFSET;
    return MXL_STATUS_OK;
}

void
PosixFlowWriter::flowRead()
{
    if ( _flowData && _flowData->flow )
    {
        auto flow = _flowData->flow->get();
        mxlGetTime( &flow->info.lastReadTime );
    }
}

mxlStatus
PosixFlowWriter::commit( const GrainInfo *in_grainInfo )
{
    if ( in_grainInfo == nullptr )
    {
        return MXL_ERR_INVALID_ARG;
    }

    auto flow = _flowData->flow->get();
    flow->info.headIndex = _currentIndex;

    uint32_t offset = _currentIndex % flow->info.grainCount;
    auto dst = &_flowData->grains.at( offset )->get()->info;
    std::memcpy( dst, in_grainInfo, sizeof( GrainInfo ) );

    /// Mark as complete (all slices are written)s
    dst->validSliceCount = in_grainInfo->sliceCount;

    mxlGetTime( &flow->info.lastWriteTime );
    _currentIndex = MXL_UNDEFINED_OFFSET;

    // Let readers know that the head has moved.
    // This will trigger an inotify event, which will wake up the readers.
    _flowData->flow->touch();

    return MXL_STATUS_OK;
}

PosixFlowWriter::~PosixFlowWriter()
{
    PosixFlowWriter::close();
}

mxlStatus
PosixFlowWriter::incrementSlice( GrainInfo *out_grainInfo )
{
    // Check that a grain has been opened
    if ( _currentIndex == MXL_UNDEFINED_OFFSET )
    {
        return MXL_ERR_OUT_OF_RANGE;
    }

    if ( _flowData )
    {
        auto flow = _flowData->flow->get();
        auto grain = _flowData->grains.at( _currentIndex % flow->info.grainCount )->get();

        /// Check if we are are already complete
        if ( grain->info.validSliceCount >= grain->info.sliceCount )
        {
            return MXL_ERR_OUT_OF_RANGE;
        }
        else
        {
            grain->info.validSliceCount++;
            *out_grainInfo = grain->info;

            // Update the head index.  The head will now point to a potentially partial grain
            flow->info.headIndex = _currentIndex;

            // Let readers know that something has changed.
            // This will trigger an inotify event, which will wake up the readers.
            _flowData->flow->touch();

            return MXL_STATUS_OK;
        }
    }
    else
    {
        return MXL_ERR_UNKNOWN;
    }
}

} // namespace mxl::lib
