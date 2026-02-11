// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file PosixContinuousFlowWriter.cpp
 * @brief Audio sample writer with batched futex signaling
 *
 * Writes audio samples to strided per-channel ring buffers. Implements intelligent
 * batching to reduce futex wake overhead: only signals readers when sync batch
 * boundaries are crossed, not on every commit.
 *
 * Batching logic (signalCompletedBatch):
 * - Tracks last signaled batch number
 * - Signals when crossing into new batch
 * - Has early-signal threshold to avoid overshooting on next commit
 * - This dramatically reduces CPU overhead for high-rate audio streams
 */

#include "PosixContinuousFlowWriter.hpp"
#include <stdexcept>
#include <mxl/time.h>
#include "mxl-internal/Sync.hpp"

namespace mxl::lib
{
    /** Constructor: caches batch sizes and computes early-signal threshold */
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
            // Early threshold prevents overshooting sync batch on next commit
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

    /** Open samples: returns mutable pointers into ring buffer for writing. Same wraparound math as reader. */
    mxlStatus PosixContinuousFlowWriter::openSamples(std::uint64_t index, std::size_t count, mxlMutableWrappedMultiBufferSlice& payloadBufferSlices)
    {
        if (_flowData)
        {
            // Limit to half buffer to prevent overwriting data readers may still need
            if (count <= (_bufferLength / 2))
            {
                // Same ring buffer math as reader
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

    /** Commit: update head index and conditionally wake readers based on batch logic */
    mxlStatus PosixContinuousFlowWriter::commit()
    {
        if (_flowData)
        {
            auto const flow = _flowData->flow();
            flow->info.runtime.headIndex = _currentIndex;
            _currentIndex = MXL_UNDEFINED_INDEX;

            // Only wake readers if we crossed a sync batch boundary
            if (signalCompletedBatch())
            {
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

    /** Cancel: discard uncommitted sample range */
    mxlStatus PosixContinuousFlowWriter::cancel()
    {
        _currentIndex = MXL_UNDEFINED_INDEX;
        return MXL_STATUS_OK;
    }

    /**
     * Batch signaling logic: only return true when sync batch boundary is crossed.
     * This reduces futex wake overhead by batching multiple commits before signaling.
     */
    bool PosixContinuousFlowWriter::signalCompletedBatch() noexcept
    {
        auto const currentSyncSampleBatch = _currentIndex / _syncBatchSize;

        // Shouldn't happen (index going backwards)
        if (currentSyncSampleBatch < _lastSyncSampleBatch)
        {
            return false;
        }

        if (currentSyncSampleBatch == _lastSyncSampleBatch)
        {
            // Still in same batch - but check if we're close to overshooting
            if ((_currentIndex % _syncBatchSize) > _earlySyncThreshold)
            {
                // Signal early to prevent overshooting on next commit
                _lastSyncSampleBatch = currentSyncSampleBatch + 1U;
                return true;
            }
            else
            {
                return false;
            }
        }
        else
        {
            // Crossed into new batch - signal!
            _lastSyncSampleBatch = currentSyncSampleBatch;
            return true;
        }
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
