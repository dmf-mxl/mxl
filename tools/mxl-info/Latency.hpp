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
#include <mxl/rational.h>
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
     * @brief Get the longest time waitForNextCommit() waits for the grain or sample after the head index.
     * @param[in] info Flow info.
     * @return One history duration in ns, rounded to the nearest ns. 0 when the history is empty, so the wait does
     *         not block. std::nullopt when the grain rate is not valid.
     */
    [[nodiscard]]
    inline std::optional<std::uint64_t> waitTimeoutNs(::mxlFlowInfo const& info) noexcept
    {
        // mxlIndexToTimestamp returns MXL_UNDEFINED_INDEX when it cannot convert with the rate.
        auto const timeoutNs = ::mxlIndexToTimestamp(&info.config.common.grainRate, historyLength(info));
        if (timeoutNs == MXL_UNDEFINED_INDEX)
        {
            return std::nullopt;
        }
        return timeoutNs;
    }

    /// Runtime info read after a commit, with the time at which the reader saw that commit.
    struct CommitSample
    {
        ::mxlFlowRuntimeInfo runtime; ///< Runtime info read after the wait returned.
        std::uint64_t nowNs;          ///< TAI time in ns, taken right after the wait returned.
    };

    /**
     * @brief Wait for the grain or sample at @p index to be committed, without reading it.
     * @param[in] instance Instance that created @p reader.
     * @param[in] reader Reader open on a discrete or continuous flow.
     * @param[in] rate Grain or sample rate of the flow.
     * @param[in] index Grain or sample index to wait for.
     * @param[in] timeoutNs Longest time to wait, in ns.
     * @return The status of the wait, or of the first group call that fails.
     */
    [[nodiscard]]
    inline ::mxlStatus waitForIndex(::mxlInstance instance, ::mxlFlowReader reader, ::mxlRational const& rate, std::uint64_t index,
        std::uint64_t timeoutNs) noexcept
    {
        // A successful read of a discrete flow updates the flow's last read time, which would make mxl-info look like
        // a consumer. A synchronization group waits for the data without reading it.
        auto group = ::mxlFlowSynchronizationGroup{nullptr};
        if (auto const status = ::mxlCreateFlowSynchronizationGroup(instance, &group); status != MXL_STATUS_OK)
        {
            return status;
        }
        auto status = ::mxlFlowSynchronizationGroupAddReader(group, reader);
        if (status == MXL_STATUS_OK)
        {
            // The group converts the timestamp back to an index at the same rate. Both conversions round to the
            // nearest value, so the group waits for this index.
            status = ::mxlFlowSynchronizationGroupWaitForDataAt(group, ::mxlIndexToTimestamp(&rate, index), timeoutNs);
        }
        // The wait status is the result. If the release fails, the instance frees the group when it is destroyed.
        (void)::mxlReleaseFlowSynchronizationGroup(instance, group);
        return status;
    }

    /**
     * @brief Wait until the grain or sample after the head index in @p info arrives, then read the runtime info again.
     *        A grain arrives when all its slices are committed. A sample arrives with the batch that contains it.
     * @param[in] instance Instance that created @p reader.
     * @param[in] reader Reader open on the flow.
     * @param[in] info Flow info read before the wait. Its head index is the starting point.
     * @return The sample taken after the wait. std::nullopt for event flows, when the grain or sample does not
     *         arrive within one history duration, or when a read fails.
     */
    inline std::optional<CommitSample> waitForNextCommit(::mxlInstance instance, ::mxlFlowReader reader, ::mxlFlowInfo const& info)
    {
        // The format helpers take int. Format values are small enumerators, so the cast keeps the value.
        auto const format = static_cast<int>(info.config.common.format);
        if (!::mxlIsDiscreteDataFormat(format) && !::mxlIsContinuousDataFormat(format))
        {
            return std::nullopt;
        }

        auto const timeoutNs = waitTimeoutNs(info);
        if (!timeoutNs.has_value())
        {
            return std::nullopt;
        }

        auto const nextIndex = info.runtime.headIndex + 1U;
        auto const status = waitForIndex(instance, reader, info.config.common.grainRate, nextIndex, *timeoutNs);
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
