#include "PosixFlowWriter.hpp"
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <sys/stat.h>
#include "Flow.hpp"
#include "FlowManager.hpp"
#include "SharedMemory.hpp"
#include "Sync.hpp"

namespace mxl::lib
{
    PosixFlowWriter::PosixFlowWriter(FlowManager::ptr manager, uuids::uuid const& flowId)
        : FlowWriter{flowId}
        , _manager{manager}
        , _flowData{}
        , _currentIndex{MXL_UNDEFINED_INDEX}
    {}

    bool PosixFlowWriter::open()
    {
        auto const& flowId = getId();
        _flowData = _manager->openFlow(flowId, AccessMode::READ_WRITE);
        return (_flowData != nullptr);
    }

    FlowInfo PosixFlowWriter::getFlowInfo()
    {
        if (_flowData)
        {
            return _flowData->flow.get()->info;
        }
        else
        {
            throw std::runtime_error("No open flow.");
        }
    }

    mxlStatus PosixFlowWriter::openGrain(std::uint64_t in_index, GrainInfo* out_grainInfo, std::uint8_t** out_payload)
    {
        if (_flowData)
        {
            std::uint32_t offset = in_index % _flowData->flow.get()->info.discrete.grainCount;

            auto grain = _flowData->grains.at(offset).get();
            *out_grainInfo = grain->header.info;
            *out_payload = reinterpret_cast<std::uint8_t*>(grain) + MXL_GRAIN_PAYLOAD_OFFSET;
            _currentIndex = in_index;
            return MXL_STATUS_OK;
        }
        else
        {
            return MXL_ERR_UNKNOWN;
        }
    }

    mxlStatus PosixFlowWriter::cancel()
    {
        _currentIndex = MXL_UNDEFINED_INDEX;
        return MXL_STATUS_OK;
    }

    void PosixFlowWriter::flowRead()
    {
        if (_flowData && _flowData->flow)
        {
            auto flow = _flowData->flow.get();
            flow->info.common.lastReadTime = mxlGetTime();
        }
    }

    mxlStatus PosixFlowWriter::commit(GrainInfo const* in_grainInfo)
    {
        if (in_grainInfo == nullptr)
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto flow = _flowData->flow.get();
        flow->info.discrete.headIndex = _currentIndex;

        std::uint32_t offset = _currentIndex % flow->info.discrete.grainCount;
        auto dst = &_flowData->grains.at(offset).get()->header.info;
        std::memcpy(dst, in_grainInfo, sizeof(GrainInfo));
        flow->info.common.lastWriteTime = mxlGetTime();

        // If the grain is complete, reset the current index of the flow writer.
        if (in_grainInfo->grainSize == in_grainInfo->commitedSize)
        {
            _currentIndex = MXL_UNDEFINED_INDEX;
        }

        // Let readers know that the head has moved or that new data is available in a partial grain
        _flowData->flow.get()->info.discrete.syncCounter++;
        wakeAll(&_flowData->flow.get()->info.discrete.syncCounter);

        return MXL_STATUS_OK;
    }
}
