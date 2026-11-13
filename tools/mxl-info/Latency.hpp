// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Latency measurement for mxl-info: ring and history lengths, waiting for the next commit, and the
 *        level used to color the latency line.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <mxl/dataformat.h>
#include <mxl/flow.h>
#include <mxl/flowinfo.h>
#include <mxl/mxl.h>
#include <mxl/time.h>

namespace mxl::info
{
    /**
     * @brief Get the ring length of a flow in indices.
     * @param[in] info Flow info.
     * @return The grain count for discrete flows, the buffer length in samples of continuous flows, and 0 for
     *         other flows.
     */
    inline std::uint64_t ringLength(::mxlFlowInfo const& info) noexcept
    {
        auto const format = static_cast<int>(info.config.common.format);
        if (::mxlIsDiscreteDataFormat(format))
        {
            return info.config.discrete.grainCount;
        }
        if (::mxlIsContinuousDataFormat(format))
        {
            return info.config.continuous.bufferLength;
        }
        return 0;
    }

    /**
     * @brief Get the number of indices a reader can reach behind the head.
     * @param[in] info Flow info.
     * @return The grain count minus one for discrete flows, half the buffer length for continuous flows, and 0
     *         for other flows. The writer keeps one discrete grain, and only half of a continuous buffer is
     *         readable at any time.
     */
    inline std::uint64_t historyLength(::mxlFlowInfo const& info) noexcept
    {
        auto const length = ringLength(info);
        if (::mxlIsContinuousDataFormat(static_cast<int>(info.config.common.format)))
        {
            return length / 2U;
        }
        // The writer keeps the slot after the head, so readers can reach one grain less than the ring length.
        return (length > 0U) ? length - 1U : 0U;
    }

    /**
     * @brief Get the longest time waitForNextCommit() waits for a commit.
     * @param[in] info Flow info.
     * @return One history duration in ns, rounded to the nearest ns. 0 when the history is empty, so the read does
     *         not block. MXL_UNDEFINED_INDEX when the grain rate is not valid.
     */
    [[nodiscard]]
    inline std::uint64_t waitTimeoutNs(::mxlFlowInfo const& info) noexcept
    {
        return ::mxlIndexToTimestamp(&info.config.common.grainRate, historyLength(info));
    }

    /// Runtime info read after a commit, with the time at which the reader saw that commit.
    struct CommitSample
    {
        ::mxlFlowRuntimeInfo runtime; ///< Runtime info read after the wait returned.
        std::uint64_t nowNs;          ///< TAI time in ns, taken right after the wait returned.
    };

    /**
     * @brief Wait for the grain or sample at @p index to be committed.
     * @param[in] reader Reader open on the flow.
     * @param[in] format Data format of the flow. It must be a discrete or a continuous format.
     * @param[in] index Grain or sample index to wait for.
     * @param[in] timeoutNs Longest time to wait, in ns.
     * @return The status of the read.
     */
    [[nodiscard]]
    inline ::mxlStatus waitForIndex(::mxlFlowReader reader, int format, std::uint64_t index, std::uint64_t timeoutNs) noexcept
    {
        // The public functions reject null out pointers. The values written here are not used.
        if (::mxlIsDiscreteDataFormat(format))
        {
            auto grainInfo = ::mxlGrainInfo{};
            auto* payload = static_cast<std::uint8_t*>(nullptr);
            return ::mxlFlowReaderGetGrain(reader, index, timeoutNs, &grainInfo, &payload);
        }
        auto slices = ::mxlWrappedMultiBufferSlice{};
        return ::mxlFlowReaderGetSamples(reader, index, 1U, timeoutNs, &slices);
    }

    /**
     * @brief Wait until the writer commits past the head index in @p info, then read the runtime info again.
     * @param[in] reader Reader open on the flow.
     * @param[in] info Flow info read before the wait. Its head index is the starting point.
     * @return The sample taken after the commit. std::nullopt for event flows, when no commit arrives within
     *         one history duration, or when a read fails.
     */
    inline std::optional<CommitSample> waitForNextCommit(::mxlFlowReader reader, ::mxlFlowInfo const& info)
    {
        // The format helpers take int. Format values are small enumerators, so the cast keeps the value.
        auto const format = static_cast<int>(info.config.common.format);
        if (!::mxlIsDiscreteDataFormat(format) && !::mxlIsContinuousDataFormat(format))
        {
            return std::nullopt;
        }

        auto const timeoutNs = waitTimeoutNs(info);
        if (timeoutNs == MXL_UNDEFINED_INDEX)
        {
            return std::nullopt;
        }

        auto const nextIndex = info.runtime.headIndex + 1U;
        auto const status = waitForIndex(reader, format, nextIndex, timeoutNs);
        auto const nowNs = ::mxlGetTime();

        // TOO_LATE means the writer moved more than a ring length past the nextIndex during the wait.
        // The head still moved, so the sample is valid.
        if ((status != MXL_STATUS_OK) && (status != MXL_ERR_OUT_OF_RANGE_TOO_LATE))
        {
            return std::nullopt;
        }

        auto runtime = ::mxlFlowRuntimeInfo{};
        if (::mxlFlowReaderGetRuntimeInfo(reader, &runtime) != MXL_STATUS_OK)
        {
            return std::nullopt;
        }
        return CommitSample{.runtime = runtime, .nowNs = nowNs};
    }

    /// Level of a latency value compared to the reader history.
    enum class LatencyLevel
    {
        Normal,  ///< At most half of the history.
        Warning, ///< More than half of the history, up to the full history.
        Error,   ///< More than the history, or the head index is ahead of the current time.
    };

    /**
     * @brief Compare a latency with the reader history.
     * @param[in] latency Latency in grains or samples.
     * @param[in] history Number of indices a reader can reach behind the head. See historyLength().
     * @param[in] inTheFuture True when the head index is ahead of the current time.
     * @return The level of @p latency.
     */
    [[nodiscard]]
    constexpr LatencyLevel latencyLevel(std::uint64_t latency, std::uint64_t history, bool inTheFuture) noexcept
    {
        if (inTheFuture || (latency > history))
        {
            return LatencyLevel::Error;
        }
        if (latency > history / 2U)
        {
            return LatencyLevel::Warning;
        }
        return LatencyLevel::Normal;
    }
}
