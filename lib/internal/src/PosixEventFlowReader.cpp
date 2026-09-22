// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
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
#include <fcntl.h>
#include <unistd.h>
#include <uuid.h>
#include <sys/stat.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/Flow.hpp"
#include "mxl-internal/FlowManager.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/Sync.hpp"

namespace mxl::lib
{
    namespace
    {
        /**
         * @brief Touch the flow access file after a successful read.
         * @param fd Access file descriptor; a negative descriptor fails harmlessly.
         * @return True if the access timestamp was updated, false if futimens failed.
         */
        bool updateFileAccessTime(int fd) noexcept
        {
            auto const times = std::array<timespec, 2>{
                {{.tv_sec = 0, .tv_nsec = UTIME_NOW}, {.tv_sec = 0, .tv_nsec = UTIME_OMIT}}
            };
            return (::futimens(fd, times.data()) == 0);
        }
    }

    PosixEventFlowReader::PosixEventFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<EventFlowData>&& data)
        : EventFlowReader{flowId, manager.getDomain()}
        , _flowData{std::move(data)}
        , _accessFileFd{-1}
        , _nextIndex{0}
    {
        _nextIndex = _flowData->oldestIndex();
        auto const accessFile = makeFlowAccessFilePath(manager.getDomain(), to_string(flowId));
        _accessFileFd = ::open(accessFile.string().c_str(), O_RDWR | O_CLOEXEC);

        // Opening the access file may fail if the domain is in a read only volume.
        // we can still execute properly but the 'lastReadTime' will never be updated.
        // Ignore failures.
    }

    PosixEventFlowReader::~PosixEventFlowReader()
    {
        if (_accessFileFd != -1)
        {
            if (::close(_accessFileFd) != 0)
            {
                auto const error = errno;
                MXL_ERROR("Failed to close access file fd: {}", std::strerror(error));
            }
            _accessFileFd = -1;
        }
    }

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
        return _flowData->infoSnapshot();
    }

    mxlFlowConfigInfo PosixEventFlowReader::getFlowConfigInfo() const
    {
        return getFlowData().flowInfo()->config;
    }

    mxlFlowRuntimeInfo PosixEventFlowReader::getFlowRuntimeInfo() const
    {
        return _flowData->runtimeSnapshot();
    }

    std::vector<std::uint8_t>& PosixEventFlowReader::threadSnapshot()
    {
        auto lock = std::scoped_lock{_snapshotMutex};
        auto& snapshot = _snapshots[std::this_thread::get_id()];
        if (snapshot.empty())
        {
            snapshot.resize(_flowData->flowInfo()->config.event.eventPayloadSize);
        }
        return snapshot;
    }

    mxlStatus PosixEventFlowReader::getEvent(Timepoint deadline, mxlEventInfo* info, std::uint8_t** payload)
    {
        auto flow = _flowData->flow();
        auto sync = std::atomic_ref{flow->state.syncCounter};
        auto& snapshot = threadSnapshot();
        auto retry = bool{};
        while (true)
        {
            // Bound retries when other threads repeatedly win the cursor CAS.
            // A nonblocking call still gets one attempt even with an expired deadline.
            if (retry && (currentTime(Clock::Realtime) >= deadline))
            {
                return MXL_ERR_OUT_OF_RANGE_TOO_EARLY;
            }
            retry = true;
            auto const previous = sync.load(std::memory_order_acquire);
            auto next = _nextIndex.load(std::memory_order_acquire);
            auto const oldest = _flowData->oldestIndex();
            if (next < oldest)
            {
                if (_nextIndex.compare_exchange_weak(next, oldest, std::memory_order_acq_rel))
                {
                    return MXL_ERR_OUT_OF_RANGE_TOO_LATE;
                }
                continue;
            }
            auto received = mxlEventInfo{};
            auto const result = _flowData->ring().read(next, received, snapshot.data());
            if (result == EventRingBuffer::ReadResult::Ready)
            {
                if (!_nextIndex.compare_exchange_weak(next, next + 1, std::memory_order_acq_rel))
                {
                    continue; // Another thread consumed this event through our handle.
                }
                *info = received;
                *payload = snapshot.data();
                (void)updateFileAccessTime(_accessFileFd);
                return MXL_STATUS_OK;
            }
            if (result == EventRingBuffer::ReadResult::Overwritten)
            {
                auto const resume = std::max(next + 1, _flowData->oldestIndex());
                if (_nextIndex.compare_exchange_weak(next, resume, std::memory_order_acq_rel))
                {
                    return MXL_ERR_OUT_OF_RANGE_TOO_LATE;
                }
                continue;
            }
            if (result == EventRingBuffer::ReadResult::Invalid)
            {
                return MXL_ERR_FLOW_INVALID;
            }
            if (!isFlowValidImpl())
            {
                return MXL_ERR_FLOW_INVALID;
            }
            if (!waitUntilChanged(&flow->state.syncCounter, previous, deadline))
            {
                return MXL_ERR_OUT_OF_RANGE_TOO_EARLY;
            }
            retry = false; // Inspect a notification before deciding it timed out.
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

        using FileStatus = struct stat;
        auto st = FileStatus{};
        if (::stat(flowDataPath.string().c_str(), &st) != 0)
        {
            return false;
        }
        return (st.st_ino == flowState->inode);
    }
}
