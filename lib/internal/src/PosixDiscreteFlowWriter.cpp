// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "PosixDiscreteFlowWriter.hpp"
#include <cstdint>
#include <stdexcept>
#include <fcntl.h>
#include <uuid.h>
#include <sys/stat.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/Flow.hpp"
#include "mxl-internal/FlowManager.hpp"
#include "mxl-internal/Sync.hpp"
#include "mxl-internal/Timing.hpp"

namespace mxl::lib
{
    PosixDiscreteFlowWriter::PosixDiscreteFlowWriter(FlowManager const&, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data,
        std::shared_ptr<DomainWatcher> const& watcher)
        : DiscreteFlowWriter{flowId}
        , _flowData{std::move(data)}
        , _currentIndex{MXL_UNDEFINED_INDEX}
        , _watcher(watcher)
    {
        _watcher->addFlow(this, flowId);
    }

    PosixDiscreteFlowWriter::~PosixDiscreteFlowWriter()
    {
        try
        {
            _watcher->removeFlow(this, _flowData->flowInfo()->config.common.id);
        }
        catch (...)
        {
            MXL_ERROR("Bug: exception while removing flow writer from watcher in destructor");
        }
    }

    FlowData const& PosixDiscreteFlowWriter::getFlowData() const
    {
        if (_flowData)
        {
            return *_flowData;
        }
        throw std::runtime_error("No open flow.");
    }

    mxlFlowInfo PosixDiscreteFlowWriter::getFlowInfo() const
    {
        return *getFlowData().flowInfo();
    }

    mxlFlowConfigInfo PosixDiscreteFlowWriter::getFlowConfigInfo() const
    {
        return getFlowData().flowInfo()->config;
    }

    mxlFlowRuntimeInfo PosixDiscreteFlowWriter::getFlowRuntimeInfo() const
    {
        return getFlowData().flowInfo()->runtime;
    }

    mxlGrainInfo PosixDiscreteFlowWriter::getGrainInfo(std::uint64_t in_index) const
    {
        auto& flowData = static_cast<DiscreteFlowData const&>(getFlowData());
        auto const offset = in_index % flowData.flowInfo()->config.discrete.grainCount;
        return flowData.grainAt(offset)->header.info;
    }

    mxlStatus PosixDiscreteFlowWriter::openGrain(std::uint64_t in_index, mxlGrainInfo* out_grainInfo, std::uint8_t** out_payload)
    {
        if (_flowData)
        {
            auto offset = in_index % _flowData->flowInfo()->config.discrete.grainCount;
            auto const grain = _flowData->grainAt(offset);
            grain->header.info.index = in_index; // Set the absolute grain index associated to that ring buffer entry
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

    bool PosixDiscreteFlowWriter::isExclusive() const
    {
        if (!_flowData)
        {
            return false;
        }

        return _flowData->isExclusive();
    }

    bool PosixDiscreteFlowWriter::makeExclusive()
    {
        if (!_flowData)
        {
            return false;
        }

        return _flowData->makeExclusive();
    }

    mxlStatus PosixDiscreteFlowWriter::commit(mxlGrainInfo const& mxlGrainInfo)
    {
        if (_flowData)
        {
            if (mxlGrainInfo.index != _currentIndex)
            {
                return MXL_ERR_INVALID_ARG;
            }

            auto const flow = _flowData->flow();
            flow->info.runtime.headIndex = _currentIndex;

            auto const offset = _currentIndex % flow->info.config.discrete.grainCount;
            *_flowData->grainInfoAt(offset) = mxlGrainInfo;
            flow->info.runtime.lastWriteTime = currentTime(mxl::lib::Clock::TAI).value;

            // If the grain is complete, reset the current index of the flow writer.
            if (mxlGrainInfo.validSlices == mxlGrainInfo.totalSlices)
            {
                _currentIndex = MXL_UNDEFINED_INDEX;
            }

            // Let readers know that the head has moved or that new data is available in a partial grain
            flow->state.syncCounter++;
            wakeAll(&flow->state.syncCounter);

            return MXL_STATUS_OK;
        }
        return MXL_ERR_UNKNOWN;
    }
}
