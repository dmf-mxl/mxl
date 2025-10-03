// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "PosixDiscreteFlowWriter.hpp"
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
    PosixDiscreteFlowWriter::PosixDiscreteFlowWriter(FlowManager const&, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data)
        : DiscreteFlowWriter{flowId}
        , _flowData{std::move(data)}
        , _currentIndex{MXL_UNDEFINED_INDEX}
    {}

    mxlFlowInfo PosixDiscreteFlowWriter::getFlowInfo()
    {
        if (_flowData)
        {
            return *_flowData->flowInfo();
        }
        throw std::runtime_error("No open flow.");
    }

    mxlStatus PosixDiscreteFlowWriter::openGrain(std::uint64_t in_index, mxlGrainInfo* out_grainInfo, std::uint8_t** out_payload)
    {
        if (_flowData)
        {
            auto offset = in_index % _flowData->flowInfo()->discrete.grainCount;
            auto const grain = _flowData->grainAt(offset);
            *out_grainInfo = grain->header.info;
            *out_payload = reinterpret_cast<std::uint8_t*>(&grain->header + 1);
            _currentIndex = in_index;
            return MXL_STATUS_OK;
        }
        return MXL_ERR_UNKNOWN;
    }

    mxlStatus PosixDiscreteFlowWriter::cancel()
    {
        _currentIndex = MXL_UNDEFINED_INDEX;
        return MXL_STATUS_OK;
    }

    void PosixDiscreteFlowWriter::flowRead()
    {
        if (_flowData)
        {
            _flowData->flowInfo()->common.lastReadTime = mxlGetTime();
        }
    }

    mxlStatus PosixDiscreteFlowWriter::commit(mxlGrainInfo const& mxlGrainInfo)
    {
        if (_flowData)
        {
            auto const flowInfo = _flowData->flowInfo();
            flowInfo->discrete.headIndex = _currentIndex;

            auto const offset = _currentIndex % flowInfo->discrete.grainCount;
            *_flowData->grainInfoAt(offset) = mxlGrainInfo;
            flowInfo->common.lastWriteTime = mxlGetTime();

            // If the grain is complete, reset the current index of the flow writer.
            if (mxlGrainInfo.grainSize == mxlGrainInfo.commitedSize)
            {
                _currentIndex = MXL_UNDEFINED_INDEX;
            }

            // Let readers know that the head has moved or that new data is available in a partial grain
            flowInfo->discrete.syncCounter++;
            wakeAll(&flowInfo->discrete.syncCounter);

            return MXL_STATUS_OK;
        }
        return MXL_ERR_UNKNOWN;
    }

    FlowData& PosixDiscreteFlowWriter::getFlowData()
    {
        if (_flowData)
        {
            return *_flowData;
        }
        throw std::runtime_error("no open flow");
    }
}
