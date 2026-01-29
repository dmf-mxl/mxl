// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "PosixContinuousFlowWriter.hpp"
#include <stdexcept>
#include <mxl/time.h>
#include "mxl-internal/Sync.hpp"

namespace mxl::lib
{
    PosixContinuousFlowWriter::PosixContinuousFlowWriter(FlowManager const&, uuids::uuid const& flowId, std::unique_ptr<ContinuousFlowData>&& data)
        : ContinuousFlowWriter{flowId}
        , _flowData{std::move(data)}
        , _channelCount{_flowData->channelCount()}
        , _bufferLength{_flowData->channelBufferLength()}
        , _currentIndex{MXL_UNDEFINED_INDEX}
        , _syncBatchSize{1U}
        , _earlySyncThreshold{}
        , _lastSyncSampleBatch{}
    {
        if (_flowData)
        {
            auto const& commonFlowConfigInfo = _flowData->flowInfo()->config.common;

            auto const commitBatchSize = std::max(commonFlowConfigInfo.maxCommitBatchSizeHint, 1U);
            _syncBatchSize = std::max(commonFlowConfigInfo.maxSyncBatchSizeHint, 1U);
            _earlySyncThreshold = (_syncBatchSize >= commitBatchSize) ? (_syncBatchSize - commitBatchSize) : 0U;
        }
    }

    FlowData const& PosixContinuousFlowWriter::getFlowData() const
    {
        if (_flowData)
        {
            return *_flowData;
        }
        throw std::runtime_error("No open flow.");
    }

    mxlFlowInfo PosixContinuousFlowWriter::getFlowInfo() const
    {
        return *getFlowData().flowInfo();
    }

    mxlFlowConfigInfo PosixContinuousFlowWriter::getFlowConfigInfo() const
    {
        return getFlowData().flowInfo()->config;
    }

    mxlFlowRuntimeInfo PosixContinuousFlowWriter::getFlowRuntimeInfo() const
    {
        return getFlowData().flowInfo()->runtime;
    }

    mxlStatus PosixContinuousFlowWriter::openSamples(std::uint64_t index, std::size_t count, mxlMutableWrappedMultiBufferSlice& payloadBufferSlices)
    {
        if (_flowData)
        {
            if (count <= (_bufferLength / 2))
            {
                auto const startOffset = (index + _bufferLength - count) % _bufferLength;
                auto const endOffset = (index % _bufferLength);

                auto const firstLength = (startOffset < endOffset) ? count : _bufferLength - startOffset;
                auto const secondLength = count - firstLength;

                auto const baseBufferPtr = static_cast<std::uint8_t*>(_flowData->channelData());
                auto const sampleWordSize = _flowData->sampleWordSize();

                payloadBufferSlices.base.fragments[0].pointer = baseBufferPtr + sampleWordSize * startOffset;
                payloadBufferSlices.base.fragments[0].size = sampleWordSize * firstLength;

                payloadBufferSlices.base.fragments[1].pointer = baseBufferPtr;
                payloadBufferSlices.base.fragments[1].size = sampleWordSize * secondLength;

                payloadBufferSlices.stride = sampleWordSize * _bufferLength;
                payloadBufferSlices.count = _channelCount;

                _currentIndex = index;

                return MXL_STATUS_OK;
            }

            return MXL_ERR_INVALID_ARG;
        }
        return MXL_ERR_UNKNOWN;
    }

    mxlStatus PosixContinuousFlowWriter::commit()
    {
        if (_flowData)
        {
            auto const flow = _flowData->flow();
            flow->info.runtime.headIndex = _currentIndex;
            _currentIndex = MXL_UNDEFINED_INDEX;

            if (signalCompletedBatch())
            {
                // Let readers know that the head has moved
                flow->state.syncCounter++;
                wakeAll(&flow->state.syncCounter);
            }

            return MXL_STATUS_OK;
        }
        else
        {
            _currentIndex = MXL_UNDEFINED_INDEX;
            return MXL_ERR_UNKNOWN;
        }
    }

    mxlStatus PosixContinuousFlowWriter::cancel()
    {
        _currentIndex = MXL_UNDEFINED_INDEX;
        return MXL_STATUS_OK;
    }

    bool PosixContinuousFlowWriter::signalCompletedBatch() noexcept
    {
        auto const currentSyncSampleBatch = _currentIndex / _syncBatchSize;
        if (currentSyncSampleBatch < _lastSyncSampleBatch)
        {
            return false;
        }
        if (currentSyncSampleBatch == _lastSyncSampleBatch)
        {
            // Signal now before overshooting the maximum the next time around
            if ((_currentIndex % _syncBatchSize) > _earlySyncThreshold)
            {
                _lastSyncSampleBatch = currentSyncSampleBatch + 1U;
            }
            else
            {
                return false;
            }
        }
        else
        {
            _lastSyncSampleBatch = currentSyncSampleBatch;
        }

        return true;
    }

    bool PosixContinuousFlowWriter::isExclusive() const
    {
        if (!_flowData)
        {
            throw std::runtime_error("Flow writer not initialized");
        }

        return _flowData->isExclusive();
    }

    bool PosixContinuousFlowWriter::makeExclusive()
    {
        if (!_flowData)
        {
            throw std::runtime_error("Flow writer not initialized");
        }

        return _flowData->makeExclusive();
    }
}
