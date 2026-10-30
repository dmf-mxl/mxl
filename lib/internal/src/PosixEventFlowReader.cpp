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
        , _snapshot(_flowData->flowInfo()->config.event.eventPayloadSize)
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
                (void)updateFileAccessTime(_accessFileFd);
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
            if ((currentTime(Clock::Realtime) >= deadline) || !waitUntilChanged(&flow->state.syncCounter, previous, deadline))
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

        using FileStatus = struct stat;
        auto st = FileStatus{};
        if (::stat(flowDataPath.string().c_str(), &st) != 0)
        {
            return false;
        }
        return (st.st_ino == flowState->inode);
    }
}
