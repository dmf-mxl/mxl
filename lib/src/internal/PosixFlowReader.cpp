#include "PosixFlowReader.hpp"

#include "Flow.hpp"
#include "FlowManager.hpp"
#include "Logging.hpp"
#include "SharedMem.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid.h>

namespace mxl::lib {

namespace fs = std::filesystem;

PosixFlowReader::PosixFlowReader( FlowManager::ptr in_manager ) : _manager{ in_manager }, _accessFileFd{ -1 } {}

PosixFlowReader::~PosixFlowReader()
{
    PosixFlowReader::close();
}

bool
PosixFlowReader::open( const uuids::uuid &in_id )
{
    _flowData = _manager->openFlow( in_id, AccessMode::READ_ONLY );
    if ( _flowData )
    {
        _flowId = in_id;

        fs::path base = _manager->getDomain();
        auto accessFile = base / ( uuids::to_string( in_id ) + ".access" );
        _accessFileFd = ::open( accessFile.string().c_str(), O_RDWR );

        // Opening the access file may fail if the domain is in a read only volume.
        // we can still execute properly but the 'lastReadTime' will never be updated.
        // Ignore failures.

        return true;
    }
    else
    {
        return false;
    }
}

void
PosixFlowReader::close()
{
    if ( _flowData )
    {
        std::unique_lock lk( _grainMutex );
        _manager->closeFlow( _flowData );
        _flowData.reset();
        _flowId = uuids::uuid(); // reset to nil
        if ( _accessFileFd != -1 )
        {
            ::close( _accessFileFd );
        }
    }
}

FlowInfo
PosixFlowReader::getFlowInfo()
{
    if ( _flowData )
    {
        FlowInfo info = _flowData->flow->get()->info;
        return info;
    }
    else
    {
        throw std::runtime_error( "No open flow." );
    }
}

mxlStatus
PosixFlowReader::getGrain( uint64_t in_index, uint16_t in_timeoutMs, GrainInfo *out_grainInfo, GrainAccessor *out_accessor )
{
    mxlStatus status = MXL_ERR_TIMEOUT;

    if ( _flowData )
    {
        // Update the file times
        std::array<timespec, 2> times;
        times[0].tv_sec = 0; // Set access time to current time
        times[0].tv_nsec = UTIME_NOW;
        times[1].tv_sec = 0; // Set modification time to current time
        times[1].tv_nsec = UTIME_NOW;
        if ( ::futimens( _accessFileFd, times.data() ) == -1 )
        {
            MXL_ERROR( "Failed to update access file times" );
        }

        auto flow = _flowData->flow->get();
        uint64_t minIndex = std::max<uint64_t>( 0, flow->info.headIndex - flow->info.grainCount + 1 );

        if ( minIndex > in_index )
        {
            status = MXL_ERR_OUT_OF_RANGE;
        }
        else if ( in_index <= flow->info.headIndex )
        {
            uint32_t offset = in_index % flow->info.grainCount;
            auto grain = _flowData->grains.at( offset )->get();
            *out_grainInfo = grain->info;

            out_accessor->payload = reinterpret_cast<uint8_t *>( grain ) + MXL_GRAIN_PAYLOAD_OFFSET;

            if ( grain->info.sliceCount == 0 )
            {
                out_accessor->validSize = grain->info.grainSize;
            }
            else
            {
                out_accessor->validSize = grain->info.grainSize * grain->info.validSliceCount / grain->info.sliceCount;
            }

            out_accessor->payloadSize = grain->info.grainSize;
            status = MXL_STATUS_OK;
        }
        else
        {
            std::unique_lock lk( _grainMutex );
            if ( _grainCV.wait_for( lk, std::chrono::milliseconds{ in_timeoutMs } ) == std::cv_status::no_timeout )
            {
                uint32_t offset = in_index % flow->info.grainCount;
                auto grain = _flowData->grains.at( offset )->get();
                *out_grainInfo = grain->info;
                out_accessor->payload = reinterpret_cast<uint8_t *>( grain ) + MXL_GRAIN_PAYLOAD_OFFSET;

                if ( grain->info.sliceCount == 0 )
                {
                    out_accessor->validSize = grain->info.grainSize;
                }
                else
                {
                    out_accessor->validSize = grain->info.grainSize * grain->info.validSliceCount / grain->info.sliceCount;
                }

                out_accessor->payloadSize = grain->info.grainSize;
                status = MXL_STATUS_OK;
            }
            else
            {
                status = MXL_ERR_TIMEOUT;
            }
        }
    }

    return status;
}

void
PosixFlowReader::grainAvailable()
{
    _grainCV.notify_all();
}

} // namespace mxl::lib
