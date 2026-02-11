// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file PosixContinuousFlowReader.cpp
 * @brief Implementation of POSIX continuous flow reader with ring buffer math
 *
 * This file implements zero-copy audio sample reading from per-channel ring buffers.
 * The key challenge is handling wraparound efficiently while maintaining zero-copy semantics.
 *
 * Ring buffer mathematics:
 * - Absolute indices never wrap (monotonically increasing)
 * - Ring position = absoluteIndex % bufferLength
 * - Valid window = [headIndex - bufferLength/2, headIndex]
 * - Half-buffer history provides read-behind tolerance
 *
 * Wraparound handling:
 * When the requested sample range wraps around the ring buffer, we return two fragments:
 *   Fragment 1: From startOffset to end of buffer
 *   Fragment 2: From start of buffer to endOffset
 *
 * Example (bufferLength=100, reading 10 samples ending at index=105):
 *   Ring positions: [96, 97, 98, 99, 0, 1, 2, 3, 4, 5]
 *   Fragment 1: bufferPtr + 96*sampleSize, length=4 samples
 *   Fragment 2: bufferPtr + 0,             length=6 samples
 *
 * Futex synchronization:
 * - Reader loads sync counter BEFORE checking head index (prevents race)
 * - If data not ready, waits on futex with remembered counter value
 * - Writer increments counter and wakes after updating head index
 * - Loop handles spurious wakeups naturally
 */

#include "PosixContinuousFlowReader.hpp"
#include <atomic>
#include <sys/stat.h>
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/Sync.hpp"

namespace mxl::lib
{
    /**
     * @brief Construct reader with flow metadata
     *
     * Caches channel count and buffer length for efficient sample access.
     */
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

    /**
     * @brief Wait for samples to be available (blocking with deadline)
     *
     * Calls the blocking getSamplesImpl with count=0 to wait without retrieving data.
     * If timeout occurs and we're still too early, check if flow is stale.
     */
    mxlStatus PosixContinuousFlowReader::waitForSamples(std::uint64_t index, Timepoint deadline) const
    {
        if (_flowData)
        {
            // Call with nullptr to skip buffer setup, count=0 means just wait
            auto const result = getSamplesImpl(index, 0U, deadline, nullptr);

            // If we timed out waiting and we're still too early, the flow might be stale
            // (deleted and recreated). Check inode to detect this condition.
            return ((result != MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || isFlowValidImpl()) ? result : MXL_ERR_FLOW_INVALID;
        }

        return MXL_ERR_UNKNOWN;
    }

    /**
     * @brief Get samples with blocking wait and deadline
     *
     * Waits for samples to be available, then returns zero-copy pointers.
     * May return fragmented buffers if wraparound occurs.
     */
    mxlStatus PosixContinuousFlowReader::getSamples(std::uint64_t index, std::size_t count, Timepoint deadline,
        mxlWrappedMultiBufferSlice& payloadBuffersSlices)
    {
        if (_flowData)
        {
            auto const result = getSamplesImpl(index, count, deadline, &payloadBuffersSlices);

            // Same staleness check as waitForSamples
            return ((result != MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || isFlowValidImpl()) ? result : MXL_ERR_FLOW_INVALID;
        }

        return MXL_ERR_UNKNOWN;
    }

    /**
     * @brief Get samples without blocking (immediate return)
     *
     * Returns immediately with success or failure. No futex wait is performed.
     */
    mxlStatus PosixContinuousFlowReader::getSamples(std::uint64_t index, std::size_t count, mxlWrappedMultiBufferSlice& payloadBuffersSlices)
    {
        if (_flowData)
        {
            auto const result = getSamplesImpl(index, count, &payloadBuffersSlices);

            // Check for staleness if we couldn't get data
            return ((result != MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || isFlowValidImpl()) ? result : MXL_ERR_FLOW_INVALID;
        }

        return MXL_ERR_UNKNOWN;
    }

    /**
     * @brief Check if flow is valid (public wrapper)
     */
    bool PosixContinuousFlowReader::isFlowValid() const
    {
        return _flowData && isFlowValidImpl();
    }

    /**
     * @brief Check if flow is valid by comparing inodes
     *
     * Detects if the flow was deleted and recreated (different inode).
     * This prevents reading stale data from an old mmap.
     */
    bool PosixContinuousFlowReader::isFlowValidImpl() const
    {
        auto const flowState = _flowData->flowState();
        auto const flowDataPath = makeFlowDataFilePath(getDomain(), to_string(getId()));

        // Stat the flow data file
        struct stat st;
        if (::stat(flowDataPath.string().c_str(), &st) != 0)
        {
            // File doesn't exist anymore
            return false;
        }

        // Compare inode - if different, flow was recreated
        return (st.st_ino == flowState->inode);
    }

    /**
     * @brief Non-blocking sample retrieval with ring buffer wraparound handling
     *
     * This is the core ring buffer logic. It:
     * 1. Checks if the requested samples are in the valid window
     * 2. Calculates ring buffer offsets accounting for wraparound
     * 3. Creates fragmented buffer slices when wraparound occurs
     *
     * Ring buffer math explained:
     * - index: The head of the range we want (most recent sample)
     * - count: Number of samples going back from index
     * - Range is: [index - count + 1, index] inclusive
     * - Valid window: [headIndex - bufferLength/2, headIndex]
     *
     * Wraparound detection:
     * - startOffset = (index - count + 1) % bufferLength
     * - endOffset = (index + 1) % bufferLength
     * - If startOffset < endOffset: no wrap, single fragment
     * - If startOffset >= endOffset: wraps, two fragments
     *
     * @return MXL_STATUS_OK if data available
     *         MXL_ERR_OUT_OF_RANGE_TOO_EARLY if not written yet
     *         MXL_ERR_OUT_OF_RANGE_TOO_LATE if overwritten
     */
    mxlStatus PosixContinuousFlowReader::getSamplesImpl(std::uint64_t index, std::size_t count,
        mxlWrappedMultiBufferSlice* payloadBuffersSlices) const
    {
        auto const headIndex = _flowData->flowInfo()->runtime.headIndex;

        // Check if we're asking for data that hasn't been written yet
        if (index <= headIndex)
        {
            // Calculate the minimum valid index (half-buffer history)
            auto const minIndex = (headIndex >= (_bufferLength / 2U)) ? (headIndex - (_bufferLength / 2U)) : std::uint64_t{0};

            // Check if requested range is within valid window
            if ((index >= minIndex) && ((index - minIndex) >= count))
            {
                // Data is available - set up zero-copy pointers if requested
                if (payloadBuffersSlices != nullptr)
                {
                    // Calculate ring buffer offsets
                    // We want samples from [index - count + 1, index] inclusive
                    auto const startOffset = (index + _bufferLength - count) % _bufferLength;
                    auto const endOffset = (index % _bufferLength);

                    // Determine if wraparound occurs
                    // If startOffset < endOffset: contiguous range, no wrap
                    // If startOffset >= endOffset: wraps around, need two fragments
                    auto const firstLength = (startOffset < endOffset) ? count : _bufferLength - startOffset;
                    auto const secondLength = count - firstLength;

                    auto const baseBufferPtr = static_cast<std::uint8_t const*>(_flowData->channelData());
                    auto const sampleWordSize = _flowData->sampleWordSize();

                    // First fragment: from startOffset to end of buffer (or to endOffset if no wrap)
                    payloadBuffersSlices->base.fragments[0].pointer = baseBufferPtr + sampleWordSize * startOffset;
                    payloadBuffersSlices->base.fragments[0].size = sampleWordSize * firstLength;

                    // Second fragment: from start of buffer to endOffset (zero size if no wrap)
                    payloadBuffersSlices->base.fragments[1].pointer = baseBufferPtr;
                    payloadBuffersSlices->base.fragments[1].size = sampleWordSize * secondLength;

                    // Stride is the distance between channel 0 and channel 1 samples
                    payloadBuffersSlices->stride = sampleWordSize * _bufferLength;
                    payloadBuffersSlices->count = _channelCount;
                }

                return MXL_STATUS_OK;
            }

            // Requested samples were overwritten (too old)
            return MXL_ERR_OUT_OF_RANGE_TOO_LATE;
        }

        // Requested samples haven't been written yet (too new)
        return MXL_ERR_OUT_OF_RANGE_TOO_EARLY;
    }

    /**
     * @brief Blocking sample retrieval with futex synchronization
     *
     * This loops checking for sample availability and waiting on the futex when needed.
     * The key race-free pattern:
     * 1. Load sync counter (acquire memory order)
     * 2. Check if data is available (calls non-blocking getSamplesImpl)
     * 3. If not available, wait on futex with the remembered counter value
     * 4. If woken, loop back to step 1
     *
     * This prevents the race where:
     * - Reader checks headIndex (not ready yet)
     * - Writer updates headIndex and wakes futex
     * - Reader would miss the wake if it used current counter value
     *
     * @note The atomic_ref usage is a workaround for C++20's limitation that
     *       atomic_ref doesn't expose the wrapped object's address until C++26
     */
    mxlStatus PosixContinuousFlowReader::getSamplesImpl(std::uint64_t index, std::size_t count, Timepoint deadline,
        mxlWrappedMultiBufferSlice* payloadBuffersSlices) const
    {
        auto const flow = _flowData->flow();
        auto const syncObject = std::atomic_ref{flow->state.syncCounter};

        while (true)
        {
            // CRITICAL: Load sync counter BEFORE checking data availability
            // This prevents missing a wake notification
            auto const previousSyncCounter = syncObject.load(std::memory_order_acquire);

            // Try to get the samples (non-blocking check)
            auto const result = getSamplesImpl(index, count, payloadBuffersSlices);

            // If data is ready or error (not TOO_EARLY), return immediately
            // Otherwise, wait on futex for writer to signal
            // NOTE: Before C++26 there is no way to access the address of the object wrapped
            //      by an atomic_ref. If there were it would be much more appropriate to pass
            //      syncObject by reference here and only unwrap the underlying integer in the
            //      implementation of waitUntilChanged.
            if ((result != MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || !waitUntilChanged(&flow->state.syncCounter, previousSyncCounter, deadline))
            {
                return result;
            }

            // Woken or spurious wakeup - loop back to check again
        }
    }
}
