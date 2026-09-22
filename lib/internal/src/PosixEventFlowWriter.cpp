// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief POSIX event staging, timestamp validation and publication.
 */

#include "PosixEventFlowWriter.hpp"
#include <cstdint>
#include <atomic>
#include <stdexcept>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/Flow.hpp"
#include "mxl-internal/FlowManager.hpp"
#include "mxl-internal/Sync.hpp"
#include "mxl-internal/Timing.hpp"

namespace mxl::lib
{
    PosixEventFlowWriter::PosixEventFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<EventFlowData>&& data,
        std::shared_ptr<DomainWatcher> const& watcher)
        : EventFlowWriter{flowId, manager.getDomain()}
        , _flowData{std::move(data)}
        , _open{false}
        , _lastTimestamp{0}
        , _payload(_flowData->flowInfo()->config.event.eventPayloadSize)
        , _watcher{watcher}
    {
        // The caller guarantees sole writer ownership, keeping the committed head
        // stable while we restore the timestamp from an existing flow.
        auto const head = EventRingBuffer::load(_flowData->flowInfo()->runtime.headIndex);
        if (head != MXL_UNDEFINED_INDEX)
        {
            auto info = mxlEventInfo{};
            if (_flowData->ring().read(head, info, _payload.data()) != EventRingBuffer::ReadResult::Ready)
            {
                throw std::runtime_error{"Invalid last committed event."};
            }
            _lastTimestamp = info.timestamp;
        }
        _watcher->addFlow(this, flowId);
    }

    PosixEventFlowWriter::~PosixEventFlowWriter()
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

    FlowData& PosixEventFlowWriter::getFlowData()
    {
        if (_flowData)
        {
            return *_flowData;
        }
        throw std::runtime_error{"No open flow."};
    }

    FlowData const& PosixEventFlowWriter::getFlowData() const
    {
        if (_flowData)
        {
            return *_flowData;
        }
        throw std::runtime_error{"No open flow."};
    }

    mxlFlowInfo PosixEventFlowWriter::getFlowInfo() const
    {
        return _flowData->infoSnapshot();
    }

    mxlFlowConfigInfo PosixEventFlowWriter::getFlowConfigInfo() const
    {
        return getFlowData().flowInfo()->config;
    }

    mxlFlowRuntimeInfo PosixEventFlowWriter::getFlowRuntimeInfo() const
    {
        return _flowData->runtimeSnapshot();
    }

    mxlStatus PosixEventFlowWriter::openEvent(mxlEventInfo* info, std::uint8_t** payload)
    {
        if (_open)
        {
            return MXL_ERR_INVALID_ARG;
        }
        *info = mxlEventInfo{
            .version = EVENT_HEADER_VERSION,
            .size = sizeof(mxlEventInfo),
            .timestamp = 0,
            .flags = 0,
            .registryType = MXL_EVENT_REGISTRY_TYPE_MXL,
            .dataItemType = {},
            .eventSize = _flowData->flowInfo()->config.event.eventPayloadSize,
            .offset = 0,
            .complete = 1,
            .reserved = {},
        };
        *payload = _payload.data();
        _open = true;
        return MXL_STATUS_OK;
    }

    mxlStatus PosixEventFlowWriter::cancel()
    {
        if (!_open)
        {
            return MXL_ERR_INVALID_ARG;
        }
        _open = false;
        return MXL_STATUS_OK;
    }

    bool PosixEventFlowWriter::isExclusive() const
    {
        if (!_flowData)
        {
            return false;
        }

        return _flowData->isExclusive();
    }

    bool PosixEventFlowWriter::makeExclusive()
    {
        if (!_flowData)
        {
            return false;
        }

        return _flowData->makeExclusive();
    }

    mxlStatus PosixEventFlowWriter::commit(mxlEventInfo const& info)
    {
        if (!_open || info.version != EVENT_HEADER_VERSION || info.size != sizeof info || info.flags != 0 || info.timestamp < _lastTimestamp ||
            info.eventSize > _flowData->flowInfo()->config.event.eventPayloadSize)
        {
            return MXL_ERR_INVALID_ARG;
        }
        auto flow = _flowData->flow();
        auto& runtime = flow->info.runtime;
        auto const head = EventRingBuffer::load(runtime.headIndex);
        auto const index = head == MXL_UNDEFINED_INDEX ? 0 : head + 1;
        auto const status = _flowData->ring().publish(index, info, _payload.data());
        if (status != MXL_STATUS_OK)
        {
            return status;
        }
        std::atomic_ref{runtime.lastWriteTime}.store(currentTime(Clock::TAI).value, std::memory_order_release);
        std::atomic_ref{runtime.headIndex}.store(index, std::memory_order_release);
        _lastTimestamp = info.timestamp;
        _open = false;
        std::atomic_ref{flow->state.syncCounter}.fetch_add(1, std::memory_order_release);
        wakeAll(&flow->state.syncCounter);
        return MXL_STATUS_OK;
    }
}
