// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "PosixDiscreteFlowReader.hpp"
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <array>
#include <filesystem>
#include <mutex>
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
#include "mxl-internal/SharedMemory.hpp"
#include "mxl-internal/Sync.hpp"

namespace mxl::lib
{
    namespace
    {
        bool updateFileAccessTime(int fd) noexcept
        {
            auto const times = std::array<timespec, 2>{
                {{0, UTIME_NOW}, {0, UTIME_OMIT}}
            };
            return (::futimens(fd, times.data()) == 0);
        }
    }

    PosixDiscreteFlowReader::PosixDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data)
        : DiscreteFlowReader{flowId, manager.getDomain()}
        , _flowData{std::move(data)}
        , _accessFileFd{-1}
    {
        auto const accessFile = makeFlowAccessFilePath(manager.getDomain(), to_string(flowId));
        _accessFileFd = ::open(accessFile.string().c_str(), O_RDWR);

        // Opening the access file may fail if the domain is in a read only volume.
        // we can still execute properly but the 'lastReadTime' will never be updated.
        // Ignore failures.
    }

    PosixDiscreteFlowReader::~PosixDiscreteFlowReader()
    {
        if (_accessFileFd != -1)
        {
            if (::close(_accessFileFd) != 0)
            {
                auto const error = errno;
                MXL_ERROR("Failed to close access file fd: {}", ::strerror(error));
            }
            _accessFileFd = -1;
        }
    }

    FlowData const& PosixDiscreteFlowReader::getFlowData() const
    {
        if (_flowData)
        {
            return *_flowData;
        }
        throw std::runtime_error("No open flow.");
    }

    mxlFlowInfo PosixDiscreteFlowReader::getFlowInfo() const
    {
        return *getFlowData().flowInfo();
    }

    mxlFlowConfigInfo PosixDiscreteFlowReader::getFlowConfigInfo() const
    {
        return getFlowData().flowInfo()->config;
    }

    mxlFlowRuntimeInfo PosixDiscreteFlowReader::getFlowRuntimeInfo() const
    {
        return getFlowData().flowInfo()->runtime;
    }

    mxlStatus PosixDiscreteFlowReader::waitForGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline) const
    {
        auto result = MXL_ERR_UNKNOWN;
        if (_flowData)
        {
            result = getGrainImpl(in_index, in_minValidSlices, in_deadline, nullptr, nullptr);
            if (result == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
            {
                // If we were ultimately too early, even with blocking for a certain amount
                // of time it could very well be that we're operating on a stale flow, so we
                // use the opportunity to check whether it's valid.
                if (!isFlowValidImpl())
                {
                    result = MXL_ERR_FLOW_INVALID;
                }
            }
        }
        return result;
    }

    mxlStatus PosixDiscreteFlowReader::getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline,
        mxlGrainInfo* out_grainInfo, std::uint8_t** out_payload)
    {
        if (!_flowData)
        {
            return MXL_ERR_UNKNOWN;
        }
        if ((out_payload != nullptr) && !hasHostPayload())
        {
            return MXL_ERR_UNSUPPORTED_OPERATION;
        }

        auto slot = std::size_t{0};
        auto const result = completeRead(getGrainImpl(in_index, in_minValidSlices, in_deadline, out_grainInfo, &slot));
        if ((result == MXL_STATUS_OK) && (out_payload != nullptr))
        {
            *out_payload = hostPayloadAt(slot);
        }
        return result;
    }

    mxlStatus PosixDiscreteFlowReader::getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, mxlGrainInfo* out_grainInfo,
        std::uint8_t** out_payload)
    {
        if (!_flowData)
        {
            return MXL_ERR_UNKNOWN;
        }
        if ((out_payload != nullptr) && !hasHostPayload())
        {
            return MXL_ERR_UNSUPPORTED_OPERATION;
        }

        auto slot = std::size_t{0};
        auto const result = completeRead(getGrainImpl(in_index, in_minValidSlices, out_grainInfo, &slot));
        if ((result == MXL_STATUS_OK) && (out_payload != nullptr))
        {
            *out_payload = hostPayloadAt(slot);
        }
        return result;
    }

    mxlStatus PosixDiscreteFlowReader::getGrainSlot(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline,
        mxlGrainInfo* out_grainInfo, std::uint32_t* out_slot)
    {
        if (!_flowData)
        {
            return MXL_ERR_UNKNOWN;
        }

        auto slot = std::size_t{0};
        auto const result = completeRead(getGrainImpl(in_index, in_minValidSlices, in_deadline, out_grainInfo, &slot));
        if ((result == MXL_STATUS_OK) && (out_slot != nullptr))
        {
            *out_slot = static_cast<std::uint32_t>(slot);
        }
        return result;
    }

    mxlGrainStorageLayout PosixDiscreteFlowReader::getStorageLayout() const
    {
        if (!_flowData)
        {
            throw std::runtime_error("No open flow.");
        }
        return _flowData->storageLayout();
    }

    mxlStatus PosixDiscreteFlowReader::mapSlots(std::uint32_t in_slotCount, mxlGrainStorage* out_slots) const
    {
        if (!_flowData)
        {
            return MXL_ERR_UNKNOWN;
        }
        return _flowData->mapSlots(in_slotCount, out_slots);
    }

    mxlStatus PosixDiscreteFlowReader::completeRead(mxlStatus in_result) const
    {
        if (in_result == MXL_STATUS_OK)
        {
            // We ignore the return value of updateFileAccessTime. It may fail if the domain is in a read-only volume.
            (void)updateFileAccessTime(_accessFileFd);
        }
        else if ((in_result == MXL_ERR_OUT_OF_RANGE_TOO_EARLY) && !isFlowValidImpl())
        {
            // If we were too early, even after blocking for a certain amount of time, we may
            // be operating on a stale flow, so we use the opportunity to check whether it's valid.
            return MXL_ERR_FLOW_INVALID;
        }
        return in_result;
    }

    bool PosixDiscreteFlowReader::hasHostPayload() const
    {
        return _flowData->payloadStorage().type() == MXL_PAYLOAD_STORAGE_HOST_POINTER;
    }

    std::uint8_t* PosixDiscreteFlowReader::hostPayloadAt(std::size_t in_slot) const
    {
        return static_cast<std::uint8_t*>(_flowData->payloadStorage().slotStorage(in_slot).host.pointer);
    }

    mxlStatus PosixDiscreteFlowReader::getGrainImpl(std::uint64_t in_index, std::uint16_t in_minValidSlices, mxlGrainInfo* out_grainInfo,
        std::size_t* out_slot) const
    {
        auto result = MXL_ERR_UNKNOWN;
        auto const flow = _flowData->flow();
        if (auto const headIndex = flow->info.runtime.headIndex; in_index <= headIndex)
        {
            auto const grainCount = flow->info.config.discrete.grainCount;

            // Reserve the tail position for the writer. The +2U advances
            // minIndex one position past the tail. Since the tail and
            // headIndex+1 alias the same physical ring-buffer position,
            // this hides uncommitted headIndex+1 write state from
            // readers.
            auto const minIndex = (headIndex >= grainCount) ? (headIndex - grainCount + 2U) : std::uint64_t{0};

            if (in_index >= minIndex)
            {
                auto const offset = in_index % grainCount;
                auto const grain = _flowData->grainAt(offset);

                if ((grain->header.info.validSlices >= std::min(in_minValidSlices, grain->header.info.totalSlices)) ||
                    ((grain->header.info.flags & MXL_GRAIN_FLAG_INVALID) != 0))
                {
                    if (out_grainInfo != nullptr)
                    {
                        *out_grainInfo = grain->header.info;
                    }
                    if (out_slot != nullptr)
                    {
                        *out_slot = offset;
                    }

                    result = MXL_STATUS_OK;
                }
                else
                {
                    result = MXL_ERR_OUT_OF_RANGE_TOO_EARLY;
                }
            }
            else
            {
                result = MXL_ERR_OUT_OF_RANGE_TOO_LATE;
            }
        }
        else
        {
            result = MXL_ERR_OUT_OF_RANGE_TOO_EARLY;
        }

        return result;
    }

    mxlStatus PosixDiscreteFlowReader::getGrainImpl(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline,
        mxlGrainInfo* out_grainInfo, std::size_t* out_slot) const
    {
        auto const flow = _flowData->flow();
        auto const syncObject = std::atomic_ref{flow->state.syncCounter};
        while (true)
        {
            // We remember the sync counter before checking the head index, otherwise we would introduce a race condition:
            // 1. We check the header index, data won't be available yet.
            // 2. Writer writes the data and updates the counter.
            // 3. If we used the current value of the counter for the futex, we would delay everything by 1 grain.
            auto const previousSyncCounter = syncObject.load(std::memory_order_acquire);
            auto const result = getGrainImpl(in_index, in_minValidSlices, out_grainInfo, out_slot);
            // NOTE: Before C++26 there is no way to access the address of the object wrapped
            //      by an atomic_ref. If there were it would be much more appropriate to pass
            //      syncObject by reference here and only unwrap the underlying integer in the
            //      implementation of waitUntilChanged.
            if ((result != MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || !waitUntilChanged(&flow->state.syncCounter, previousSyncCounter, in_deadline))
            {
                return result;
            }
        }
    }

    bool PosixDiscreteFlowReader::isFlowValid() const
    {
        return _flowData && isFlowValidImpl();
    }

    bool PosixDiscreteFlowReader::isFlowValidImpl() const
    {
        auto const flowState = _flowData->flowState();
        auto const flowDataPath = makeFlowDataFilePath(getDomain(), to_string(getId()));

        struct stat st;
        if (::stat(flowDataPath.string().c_str(), &st) != 0)
        {
            return false;
        }
        return (st.st_ino == flowState->inode);
    }
}
