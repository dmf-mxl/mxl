// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief POSIX event reads, queue advancement and access notifications.
 */

#include "PosixEventFlowReader.hpp"
#include <cstdint>
#include <ctime>
#include <array>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/Flow.hpp"
#include "mxl-internal/FlowManager.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"

namespace mxl::lib
{
    PosixEventFlowReader::PosixEventFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<EventFlowData>&& data)
        : EventFlowReader{flowId, manager.getDomain()}
        , _flowData{std::move(data)}
        , _accessFile{makeFlowAccessFilePath(manager.getDomain(), to_string(flowId))}
        , _nextIndex{0}
        , _snapshot(_flowData->flowInfo()->config.event.eventPayloadSize)
    {
        _nextIndex = _flowData->oldestIndex();
    }

    PosixEventFlowReader::~PosixEventFlowReader() = default;

    FlowData const& PosixEventFlowReader::getFlowData() const
    {
        if (_flowData)
        {
            return *_flowData;
        }
        throw std::runtime_error{"No open flow."};
    }

    mxlFlowInfo PosixEventFlowReader::getFlowInfo() const
    {
        return *getFlowData().flowInfo();
    }

    mxlFlowConfigInfo PosixEventFlowReader::getFlowConfigInfo() const
    {
        return getFlowData().flowInfo()->config;
    }

    mxlFlowRuntimeInfo PosixEventFlowReader::getFlowRuntimeInfo() const
    {
        return getFlowData().flowInfo()->runtime;
    }

    mxlStatus PosixEventFlowReader::getEvent(Timepoint deadline, mxlEventInfo* info, std::uint8_t** payload)
    {
        auto flow = _flowData->flow();
        auto sync = std::atomic_ref{flow->state.syncCounter};
        while (true)
        {
            auto const previous = sync.load(std::memory_order_acquire);
            auto const oldest = _flowData->oldestIndex();
            if (_nextIndex < oldest)
            {
                _nextIndex = oldest;
                return MXL_ERR_OUT_OF_RANGE_TOO_LATE;
            }
            auto received = mxlEventInfo{};
            auto const result = _flowData->ring().read(_nextIndex, received, _snapshot.data());
            if (result == EventRingBuffer::ReadResult::Ready)
            {
                ++_nextIndex;
                *info = received;
                *payload = _snapshot.data();
                (void)_accessFile.touch();
                return MXL_STATUS_OK;
            }
            if (result == EventRingBuffer::ReadResult::Overwritten)
            {
                _nextIndex = std::max(_nextIndex + 1, _flowData->oldestIndex());
                return MXL_ERR_OUT_OF_RANGE_TOO_LATE;
            }
            if (result == EventRingBuffer::ReadResult::Invalid)
            {
                return MXL_ERR_FLOW_INVALID;
            }
            if (!isFlowValidImpl())
            {
                return MXL_ERR_FLOW_INVALID;
            }
            if ((currentTime(Clock::Realtime) >= deadline) || !_flowData->sync().waitUntilChanged(previous, deadline))
            {
                return MXL_ERR_OUT_OF_RANGE_TOO_EARLY;
            }
        }
    }

    bool PosixEventFlowReader::isFlowValid() const
    {
        return _flowData && isFlowValidImpl();
    }

    bool PosixEventFlowReader::isFlowValidImpl() const
    {
        auto const flowState = _flowData->flowState();
        auto const flowDataPath = makeFlowDataFilePath(getDomain(), to_string(getId()));

        auto const identity = fileIdentity(flowDataPath);
        return identity && (*identity == flowState->inode);
    }
}
