// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "PosixContinuousFlowReader.hpp"
#include <atomic>
#include <sys/stat.h>
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/Sync.hpp"

namespace mxl::lib
{
    PosixContinuousFlowReader::PosixContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<ContinuousFlowData>&& data)
        : ContinuousFlowReader{flowId, manager.getDomain()}
        , _flowData{std::move(data)}
        , _channelCount{_flowData->channelCount()}
        , _bufferLength{_flowData->channelBufferLength()}
    {}

    FlowData const& PosixContinuousFlowReader::getFlowData() const
    {
        if (_flowData)
        {
            return *_flowData;
        }
        throw std::runtime_error("No open flow.");
    }

    mxlFlowInfo PosixContinuousFlowReader::getFlowInfo() const
    {
        return *getFlowData().flowInfo();
    }

    mxlFlowConfigInfo PosixContinuousFlowReader::getFlowConfigInfo() const
    {
        return getFlowData().flowInfo()->config;
    }

    mxlFlowRuntimeInfo PosixContinuousFlowReader::getFlowRuntimeInfo() const
    {
        return getFlowData().flowInfo()->runtime;
    }

    mxlStatus PosixContinuousFlowReader::waitForSamples(std::uint64_t index, std::uint64_t timeoutNs) const
    {
        if (_flowData)
        {
            auto const result = getSamplesImpl(index, 0U, timeoutNs, nullptr);

            // If we were ultimately too early, even with blocking for a
            // certain amount of time it could very well be that we're
            // operating on a stale flow, so we use the opportunity to check
            // whether it's valid.
            return ((result != MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || isFlowValidImpl()) ? result : MXL_ERR_FLOW_INVALID;
        }

        return MXL_ERR_UNKNOWN;
    }

    mxlStatus PosixContinuousFlowReader::getSamples(std::uint64_t index, std::size_t count, std::uint64_t timeoutNs,
        mxlWrappedMultiBufferSlice& payloadBuffersSlices)
    {
        if (_flowData)
        {
            auto const result = getSamplesImpl(index, count, timeoutNs, &payloadBuffersSlices);

            // If we were ultimately too early, even with blocking for a
            // certain amount of time it could very well be that we're
            // operating on a stale flow, so we use the opportunity to check
            // whether it's valid.
            return ((result != MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || isFlowValidImpl()) ? result : MXL_ERR_FLOW_INVALID;
        }

        return MXL_ERR_UNKNOWN;
    }

    mxlStatus PosixContinuousFlowReader::getSamples(std::uint64_t index, std::size_t count, mxlWrappedMultiBufferSlice& payloadBuffersSlices)
    {
        if (_flowData)
        {
            auto const result = getSamplesImpl(index, count, &payloadBuffersSlices);

            // If we were too early it could very well be that we're operating
            // on a stale flow, so we use the opportunity to check whether it's
            // valid.
            return ((result != MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || isFlowValidImpl()) ? result : MXL_ERR_FLOW_INVALID;
        }

        return MXL_ERR_UNKNOWN;
    }

    bool PosixContinuousFlowReader::isFlowValid() const
    {
        return _flowData && isFlowValidImpl();
    }

    bool PosixContinuousFlowReader::isFlowValidImpl() const
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

    mxlStatus PosixContinuousFlowReader::getSamplesImpl(std::uint64_t index, std::size_t count,
        mxlWrappedMultiBufferSlice* payloadBuffersSlices) const
    {
        if (auto const headIndex = _flowData->flowInfo()->runtime.headIndex; index <= headIndex)
        {
            auto const minIndex = (headIndex >= (_bufferLength / 2U)) ? (headIndex - (_bufferLength / 2U)) : std::uint64_t{0};

            if ((index >= minIndex) && ((index - minIndex) >= count))
            {
                if (payloadBuffersSlices != nullptr)
                {
                    auto const startOffset = (index + _bufferLength - count) % _bufferLength;
                    auto const endOffset = (index % _bufferLength);

                    auto const firstLength = (startOffset < endOffset) ? count : _bufferLength - startOffset;
                    auto const secondLength = count - firstLength;

                    auto const baseBufferPtr = static_cast<std::uint8_t const*>(_flowData->channelData());
                    auto const sampleWordSize = _flowData->sampleWordSize();

                    payloadBuffersSlices->base.fragments[0].pointer = baseBufferPtr + sampleWordSize * startOffset;
                    payloadBuffersSlices->base.fragments[0].size = sampleWordSize * firstLength;

                    payloadBuffersSlices->base.fragments[1].pointer = baseBufferPtr;
                    payloadBuffersSlices->base.fragments[1].size = sampleWordSize * secondLength;

                    payloadBuffersSlices->stride = sampleWordSize * _bufferLength;
                    payloadBuffersSlices->count = _channelCount;
                }

                return MXL_STATUS_OK;
            }

            return MXL_ERR_OUT_OF_RANGE_TOO_LATE;
        }

        return MXL_ERR_OUT_OF_RANGE_TOO_EARLY;
    }

    mxlStatus PosixContinuousFlowReader::getSamplesImpl(std::uint64_t index, std::size_t count, std::uint64_t timeoutNs,
        mxlWrappedMultiBufferSlice* payloadBuffersSlices) const
    {
        auto const deadline = currentTime(Clock::Realtime) + Duration{static_cast<std::int64_t>(timeoutNs)};
        auto const flow = _flowData->flow();
        auto const syncObject = std::atomic_ref{flow->state.syncCounter};
        while (true)
        {
            auto const previousSyncCounter = syncObject.load(std::memory_order_acquire);
            auto const result = getSamplesImpl(index, count, payloadBuffersSlices);
            // NOTE: Before C++26 there is no way to access the address of the object wrapped
            //      by an atomic_ref. If there were it would be much more appropriate to pass
            //      syncObject by reference here and only unwrap the underlying integer in the
            //      implementation of waitUntilChanged.
            if ((result != MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || !waitUntilChanged(&flow->state.syncCounter, previousSyncCounter, deadline))
            {
                return result;
            }
        }
    }
}
