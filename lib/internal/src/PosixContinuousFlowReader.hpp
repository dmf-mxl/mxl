// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file PosixContinuousFlowReader.hpp
 * @brief POSIX implementation of continuous flow reader (audio)
 *
 * This file implements the reader for continuous flows (audio) using POSIX shared memory
 * and futex-based synchronization. Continuous flows store per-channel sample ring buffers
 * in a single memory-mapped file with strided layout.
 *
 * Memory layout of the channels file:
 *   Channel 0: [sample 0][sample 1][sample 2]...[sample N-1]
 *   Channel 1: [sample 0][sample 1][sample 2]...[sample N-1]
 *   ...
 *   Channel M-1: [sample 0][sample 1][sample 2]...[sample N-1]
 *
 * The layout is "strided" meaning samples are organized by channel, not interleaved.
 * This matches the typical layout expected by audio processing libraries.
 *
 * Ring buffer mechanics:
 * - Head index advances monotonically (never wraps)
 * - Ring position = index % bufferLength
 * - Valid range: [headIndex - bufferLength/2, headIndex]
 * - Half-buffer history allows for reasonable read-behind tolerance
 *
 * Zero-copy access:
 * - Reader uses PROT_READ mmap (cannot corrupt flow)
 * - Returns pointers directly into shared memory
 * - No data copies except what application does with the pointers
 *
 * Thread safety:
 * - Reader methods are const and can be called from multiple threads
 * - Futex wait/wake provides atomicity for head index updates
 */

#pragma once

#include <cstdint>
#include <memory>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/ContinuousFlowData.hpp"
#include "mxl-internal/ContinuousFlowReader.hpp"
#include "mxl-internal/FlowManager.hpp"

namespace mxl::lib
{
    class FlowManager;

    /**
     * @brief POSIX shared-memory implementation of continuous flow reader
     *
     * Reads audio samples from per-channel ring buffers stored in a single mmap'd file.
     * Provides zero-copy access to sample data with futex-based blocking for synchronization.
     *
     * This class handles:
     * - Memory-mapped file access to channel buffers
     * - Ring buffer wraparound calculations
     * - Fragmented buffer slice creation (for wraparound cases)
     * - Futex-based blocking waits with deadlines
     * - Flow validity checking via inode comparison
     */
    class PosixContinuousFlowReader final : public ContinuousFlowReader
    {
    public:
        /**
         * @brief Construct a continuous flow reader
         *
         * @param manager Reference to flow manager for domain context
         * @param flowId UUID of the flow to read
         * @param data Continuous flow metadata (ownership transferred)
         *
         * The constructor caches channel count and buffer length from flow metadata
         * for efficient access during sample retrieval.
         */
        PosixContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<ContinuousFlowData>&& data);

        /**
         * @brief Get the flow data object
         * @return Reference to the underlying flow data
         * @throws std::runtime_error if flow is not open
         */
        [[nodiscard]]
        virtual FlowData const& getFlowData() const override;

    public:
        /**
         * @brief Get complete flow information
         * @return Copy of the mxlFlowInfo structure
         */
        [[nodiscard]]
        virtual mxlFlowInfo getFlowInfo() const override;

        /**
         * @brief Get flow configuration information
         * @return Copy of the mxlFlowConfigInfo structure (includes channel count, buffer length)
         */
        [[nodiscard]]
        virtual mxlFlowConfigInfo getFlowConfigInfo() const override;

        /**
         * @brief Get flow runtime information
         * @return Copy of the mxlFlowRuntimeInfo structure (includes head index, timestamps)
         */
        [[nodiscard]]
        virtual mxlFlowRuntimeInfo getFlowRuntimeInfo() const override;

        /**
         * @brief Block waiting for samples to be available at an index
         *
         * This method waits (using futex) until the writer has advanced the head index
         * to at least the specified index, or the deadline expires.
         *
         * @param index The sample index to wait for
         * @param deadline Absolute time to stop waiting (TAI nanoseconds)
         * @return MXL_STATUS_OK if samples available, error code otherwise
         */
        virtual mxlStatus waitForSamples(std::uint64_t index, Timepoint deadline) const override;

        /**
         * @brief Get samples with blocking wait and deadline
         *
         * Waits for samples to be available, then returns pointers to them.
         * May return fragmented buffers if the requested range wraps around the ring.
         *
         * @param index Head index of the sample range (most recent sample wanted)
         * @param count Number of samples to retrieve (working backwards from index)
         * @param deadline Absolute time to stop waiting
         * @param payloadBuffersSlices Output structure to receive sample pointers
         * @return MXL_STATUS_OK if successful, error code otherwise
         */
        virtual mxlStatus getSamples(std::uint64_t index, std::size_t count, Timepoint deadline,
            mxlWrappedMultiBufferSlice& payloadBuffersSlices) override;

        /**
         * @brief Get samples without blocking (non-blocking variant)
         *
         * Returns immediately with success if samples are available, or error if not.
         * No futex wait is performed.
         *
         * @param index Head index of the sample range
         * @param count Number of samples to retrieve
         * @param payloadBuffersSlices Output structure to receive sample pointers
         * @return MXL_STATUS_OK if successful, MXL_ERR_OUT_OF_RANGE_TOO_EARLY if not ready
         */
        virtual mxlStatus getSamples(std::uint64_t index, std::size_t count, mxlWrappedMultiBufferSlice& payloadBuffersSlices) override;

    protected:
        /**
         * @brief Check if the flow is still valid (not deleted/recreated)
         *
         * Validates that the flow's data file still exists and has the same inode
         * as when the flow was opened. This detects if the flow was deleted and
         * recreated while this reader still holds a reference.
         *
         * @return true if flow is valid, false if stale
         */
        [[nodiscard]]
        virtual bool isFlowValid() const override;

    private:
        /**
         * @brief Internal validation implementation (assumes _flowData is valid)
         * @return true if flow is valid
         */
        [[nodiscard]]
        bool isFlowValidImpl() const;

        /**
         * @brief Non-blocking sample retrieval implementation
         *
         * Checks if samples are available and returns pointers if so. Handles ring
         * buffer wraparound by creating fragmented buffer slices.
         *
         * @param index Sample index to retrieve
         * @param count Number of samples
         * @param payloadBuffersSlices Output buffer (nullptr to skip buffer setup)
         * @return MXL_STATUS_OK, MXL_ERR_OUT_OF_RANGE_TOO_EARLY, or MXL_ERR_OUT_OF_RANGE_TOO_LATE
         */
        mxlStatus getSamplesImpl(std::uint64_t index, std::size_t count, mxlWrappedMultiBufferSlice* payloadBuffersSlices) const;

        /**
         * @brief Blocking sample retrieval implementation with futex wait
         *
         * Loops checking for sample availability and waiting on the futex when needed.
         * The loop handles spurious wakeups and ensures the sync counter is checked
         * before waiting to avoid race conditions.
         *
         * @param index Sample index to retrieve
         * @param count Number of samples
         * @param deadline Absolute deadline for waiting
         * @param payloadBuffersSlices Output buffer (nullptr to skip buffer setup)
         * @return MXL_STATUS_OK if successful, error code if timeout or other failure
         */
        mxlStatus getSamplesImpl(std::uint64_t index, std::size_t count, Timepoint deadline, mxlWrappedMultiBufferSlice* payloadBuffersSlices) const;

    private:
        /** Flow data containing mmap'd metadata and channel buffers */
        std::unique_ptr<ContinuousFlowData> _flowData;

        /** Cached channel count for efficient access (avoids repeated indirection) */
        std::size_t _channelCount;

        /** Cached buffer length in samples per channel (for wraparound calculations) */
        std::size_t _bufferLength;
    };
}
