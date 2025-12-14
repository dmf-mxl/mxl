// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0
//
#pragma once

#include <chrono>
#include <optional>
#include <utility>
#include <fmt/base.h>
#include <fmt/color.h>
#include "Completion.hpp"
#include "Endpoint.hpp"
#include "Event.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief Enum to select blocking or non-blocking queue read behaviour.
     */
    enum class QueueReadMode
    {
        Blocking,
        NonBlocking
    };

    /** \brief Helper to read both completion and event queues of an endpoint with blocking or non-blocking behaviour.
     *
     * \tparam qrm The queue read mode, either Blocking or NonBlocking.
     * \param ep The endpoint to read from.
     * \param timeout The timeout to use for blocking reads.
     * \return A pair of optional Completion and Event objects read from the endpoint queues.
     * \note To read only one of the queues, use readCompletionQueue() or readEventQueue().
     */
    template<QueueReadMode qrm>
    std::pair<std::optional<Completion>, std::optional<Event>> readEndpointQueues(Endpoint& ep, std::chrono::steady_clock::duration timeout)
    {
        static_assert((qrm == QueueReadMode::Blocking) || (qrm == QueueReadMode::NonBlocking), "Unsupported queue behaviour parameter");

        auto result = std::pair<std::optional<Completion>, std::optional<Event>>{};

        if constexpr (qrm == QueueReadMode::Blocking)
        {
            result = ep.readQueuesBlocking(timeout);
        }
        else if constexpr (qrm == QueueReadMode::NonBlocking)
        {
            result = ep.readQueues();
        }

        return result;
    }

    /** \brief Helper to read an event queue with blocking or non-blocking behaviour.
     *
     * \tparam qrm The queue read mode, either Blocking or NonBlocking.
     * \param eq The event queue to read from.
     * \param timeout The timeout to use for blocking reads.
     * \return An optional Event object read from the event queue.
     */
    template<QueueReadMode qrm>
    std::optional<Event> readEventQueue(EventQueue& eq, std::chrono::steady_clock::duration timeout)
    {
        static_assert((qrm == QueueReadMode::Blocking) || (qrm == QueueReadMode::NonBlocking), "Unsupported queue behaviour parameter");

        auto event = std::optional<Event>{};

        if constexpr (qrm == QueueReadMode::Blocking)
        {
            event = eq.readBlocking(timeout);
        }
        else if constexpr (qrm == QueueReadMode::NonBlocking)
        {
            event = eq.read();
        }

        return event;
    }

    /** \brief Helper to read a completion queue with blocking or non-blocking behaviour.
     *
     * \tparam qrm The queue read mode, either Blocking or NonBlocking.
     * \param eq The completion queue to read from.
     * \param timeout The timeout to use for blocking reads.
     * \return An optional Completion object read from the completion queue.
     */
    template<QueueReadMode qrm>
    std::optional<Completion> readCompletionQueue(CompletionQueue& eq, std::chrono::steady_clock::duration timeout)
    {
        static_assert((qrm == QueueReadMode::Blocking) || (qrm == QueueReadMode::NonBlocking), "Unsupported queue behaviour parameter");

        auto completion = std::optional<Completion>{};

        if constexpr (qrm == QueueReadMode::Blocking)
        {
            completion = eq.readBlocking(timeout);
        }
        else if constexpr (qrm == QueueReadMode::NonBlocking)
        {
            completion = eq.read();
        }

        return completion;
    }
}
