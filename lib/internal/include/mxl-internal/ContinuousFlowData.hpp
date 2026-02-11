// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ContinuousFlowData.hpp
 * @brief FlowData specialization for continuous flows (AUDIO)
 *
 * Continuous flows exchange sample streams via per-channel ring buffers.
 *
 * ARCHITECTURE:
 *
 *   Filesystem layout:
 *     ${domain}/${flowId}.mxl-flow/
 *       data               -- ContinuousFlowState (ring buffer metadata, sample positions)
 *       channels           -- Per-channel sample ring buffers
 *
 *   Memory layout of "channels" file:
 *     [Channel 0 buffer: bufferLength samples]
 *     [Channel 1 buffer: bufferLength samples]
 *     ...
 *     [Channel N-1 buffer: bufferLength samples]
 *
 *   Ring buffer semantics:
 *     - Each channel has its own ring buffer of bufferLength samples
 *     - Writer appends samples, incrementing sampleOffset
 *     - Ring buffer index = sampleOffset % bufferLength
 *     - Oldest samples overwritten when buffer wraps
 *     - All channels synchronized (same sampleOffset for all)
 *
 *   Why single "channels" file?
 *     - Channels always written/read together (interleaved operation)
 *     - Single mmap for all channels (efficient)
 *     - Simpler file management than separate files per channel
 *     - Cache-friendly layout (channels adjacent in memory)
 *
 * KEY DESIGN DECISIONS:
 *
 * 1. Sample word size flexibility:
 *    - Supports different sample formats (float32, int24, etc.)
 *    - Word size stored in _sampleWordSize (typically 4 for float32)
 *    - Buffer size = channelCount * bufferLength * sampleWordSize
 *
 * 2. Lazy channel buffer mapping:
 *    - "data" file opened in constructor
 *    - "channels" file opened via openChannelBuffers() call
 *    - Allows reading metadata before mapping large sample buffers
 *
 * 3. Sample format assumptions:
 *    - MXL API primarily uses float32 (4 bytes per sample)
 *    - Other formats possible (int24 packed, etc.)
 *    - Application responsible for interpreting raw bytes correctly
 */

#pragma once

#include "FlowData.hpp"

namespace mxl::lib
{
    /**
     * FlowData specialization for continuous flows (AUDIO).
     *
     * Manages:
     * - Flow "data" file mapping (ContinuousFlowState)
     * - Single "channels" file mapping (all channel ring buffers)
     * - Sample word size tracking
     *
     * Used by both ContinuousFlowReader and ContinuousFlowWriter.
     */
    class ContinuousFlowData : public FlowData
    {
    public:
        /**
         * Construct from an existing Flow mapping.
         * "channels" file not yet mapped (call openChannelBuffers() to map it).
         * @param flowSegement Existing SharedMemoryInstance<Flow> (moved)
         */
        explicit ContinuousFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept;

        /**
         * Construct by opening the "data" file.
         * "channels" file not yet mapped (call openChannelBuffers() to map it).
         * @param flowFilePath Path to ${domain}/${flowId}.mxl-flow/data
         * @param mode READ_ONLY (reader) or READ_WRITE/CREATE_READ_WRITE (writer)
         * @param lockMode Advisory lock mode (Shared for readers, Exclusive for writers)
         * @throws std::runtime_error if open/map fails
         */
        ContinuousFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode);

        /**
         * Get the number of audio channels.
         * @return Channel count from flowInfo, or 0 if flow not mapped
         */
        constexpr std::size_t channelCount() const noexcept;

        /**
         * Get the sample word size in bytes (e.g., 4 for float32).
         * @return Sample word size, or 1 if not yet determined
         */
        constexpr std::size_t sampleWordSize() const noexcept;

        /**
         * Get the ring buffer length per channel in samples.
         * @return Buffer length from flowInfo, or 0 if flow not mapped
         */
        constexpr std::size_t channelBufferLength() const noexcept;

        /**
         * Map the "channels" file (per-channel sample ring buffers).
         *
         * Called after constructing ContinuousFlowData to open the sample buffers.
         *
         * Steps:
         * 1. Read channelCount and bufferLength from flowInfo
         * 2. Calculate total buffer size = channelCount * bufferLength * sampleWordSize
         * 3. Map "channels" file with SharedMemorySegment
         * 4. If opening existing (not creating), deduce sampleWordSize from mapped size
         *
         * @param channelBuffersFilePath Path to ${domain}/${flowId}.mxl-flow/channels
         * @param sampleWordSize Sample word size in bytes (e.g., 4 for float32).
         *                       If 0 and opening existing file, deduce from mapped size.
         * @throws std::runtime_error if channelCount or bufferLength invalid
         * @throws std::runtime_error if creating with sampleWordSize==0
         */
        void openChannelBuffers(char const* channelBuffersFilePath, std::size_t sampleWordSize);

        /**
         * Get the size of the mapped channel data in bytes.
         * @return Total mapped size (channelCount * bufferLength * sampleWordSize)
         */
        constexpr std::size_t channelDataSize() const noexcept;

        /**
         * Get the length of the mapped channel data in samples (across all channels).
         * @return Total sample count (mapped size / sampleWordSize)
         */
        constexpr std::size_t channelDataLength() const noexcept;

        /**
         * Get mutable pointer to channel data (for writers).
         * @return Pointer to start of "channels" buffer, or nullptr if not mapped
         */
        constexpr void* channelData() noexcept;

        /**
         * Get const pointer to channel data (for readers).
         * @return Const pointer to start of "channels" buffer, or nullptr if not mapped
         */
        constexpr void const* channelData() const noexcept;

    private:
        /**
         * Mapping of the "channels" file (all per-channel sample ring buffers).
         *
         * Layout: [Ch0: bufferLength samples][Ch1: bufferLength samples]...[ChN-1: bufferLength samples]
         * Size: channelCount * bufferLength * sampleWordSize bytes
         * Lifecycle: Unmapped when object destroyed (RAII)
         */
        SharedMemorySegment _channelBuffers;

        /**
         * Size of one sample in bytes (e.g., 4 for float32, 3 for int24 packed).
         * Used to calculate byte offsets and buffer sizes.
         * Initialized to 1, set to actual value in openChannelBuffers().
         */
        std::size_t _sampleWordSize;
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    inline ContinuousFlowData::ContinuousFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept
        : FlowData{std::move(flowSegement)}
        , _channelBuffers{}
        , _sampleWordSize{1U}
    {}

    inline ContinuousFlowData::ContinuousFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode)
        : FlowData{flowFilePath, mode, lockMode}
        , _channelBuffers{}
        , _sampleWordSize{1U}
    {}

    constexpr std::size_t ContinuousFlowData::channelCount() const noexcept
    {
        auto const info = flowInfo();
        return (info != nullptr) ? info->config.continuous.channelCount : 0U;
    }

    constexpr std::size_t ContinuousFlowData::channelBufferLength() const noexcept
    {
        auto const info = flowInfo();
        return (info != nullptr) ? info->config.continuous.bufferLength : 0U;
    }

    constexpr std::size_t ContinuousFlowData::sampleWordSize() const noexcept
    {
        return _sampleWordSize;
    }

    inline void ContinuousFlowData::openChannelBuffers(char const* grainFilePath, std::size_t sampleWordSize)
    {
        // Validate: If creating, must provide sampleWordSize. If opening, can deduce it.
        if ((sampleWordSize != 0U) || !created())
        {
            // Read channel geometry from flow metadata
            auto const info = flowInfo();
            auto const channelCount = info->config.continuous.channelCount;
            auto const bufferLength = info->config.continuous.bufferLength;

            // Total sample count across all channels
            if (auto const buffersLength = channelCount * bufferLength; buffersLength > 0U)
            {
                // Determine mode: If we created the flow, create "channels" file. Otherwise use flow's access mode.
                auto const mode = this->created() ? AccessMode::CREATE_READ_WRITE : this->accessMode();

                // Map the "channels" file (all per-channel ring buffers in one file)
                // Size = buffersLength samples * sampleWordSize bytes/sample
                // Shared lock: Multiple readers can access, writer holds shared lock
                _channelBuffers = SharedMemorySegment{grainFilePath, mode, buffersLength * sampleWordSize, LockMode::Shared};

                // Determine actual sample word size:
                // - If provided (creating or explicit), use it
                // - If not provided (opening existing), deduce from mapped size / expected sample count
                auto const mappedSize = _channelBuffers.mappedSize();
                _sampleWordSize = (sampleWordSize != 0U) ? sampleWordSize : ((mappedSize >= buffersLength) ? (mappedSize / buffersLength) : 1U);
            }
            else
            {
                // Invalid geometry: zero channels or zero buffer length
                throw std::runtime_error{"Attempt to open channel buffer with invalid geometry."};
            }
        }
        else
        {
            // Can't create a new "channels" file without knowing sample word size
            throw std::runtime_error{"Attempt to create channel buffer with invalid sample word size."};
        }
    }

    constexpr std::size_t ContinuousFlowData::channelDataSize() const noexcept
    {
        return _channelBuffers.mappedSize();
    }

    constexpr std::size_t ContinuousFlowData::channelDataLength() const noexcept
    {
        return _channelBuffers.mappedSize() / _sampleWordSize;
    }

    constexpr void* ContinuousFlowData::channelData() noexcept
    {
        return _channelBuffers.data();
    }

    constexpr void const* ContinuousFlowData::channelData() const noexcept
    {
        return _channelBuffers.data();
    }
}
