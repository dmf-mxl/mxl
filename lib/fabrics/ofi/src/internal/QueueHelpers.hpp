#pragma once

#include <chrono>
#include <optional>
#include <utility>
#include "Completion.hpp"
#include "Endpoint.hpp"
#include "Event.hpp"

namespace mxl::lib::fabrics::ofi
{
    enum class QueueReadMode
    {
        Blocking,
        NonBlocking
    };

    template<QueueReadMode qrm>
    std::pair<std::optional<Completion>, std::optional<Event>> readEndpointQueues(Endpoint& ep, std::chrono::steady_clock::duration timeout)
    {
        std::pair<std::optional<Completion>, std::optional<Event>> result;

        if constexpr (qrm == QueueReadMode::Blocking)
        {
            result = ep.readQueuesBlocking(timeout);
        }
        else if constexpr (qrm == QueueReadMode::NonBlocking)
        {
            result = ep.readQueues();
        }
        else
        {
            static_assert(false, "Unsupported queue behaviour parameter");
        }

        return result;
    }

    template<QueueReadMode qrm>
    std::optional<Event> readEventQueue(EventQueue& eq, std::chrono::steady_clock::duration timeout)
    {
        std::optional<Event> event;

        if constexpr (qrm == QueueReadMode::Blocking)
        {
            event = eq.readBlocking(timeout);
        }
        else if constexpr (qrm == QueueReadMode::NonBlocking)
        {
            event = eq.read();
        }
        else
        {
            static_assert(false, "Unsupported queue behaviour parameter");
        }

        return event;
    }

    template<QueueReadMode qrm>
    std::optional<Completion> readCompletionQueue(CompletionQueue& eq, std::chrono::steady_clock::duration timeout)
    {
        std::optional<Completion> completion;

        if constexpr (qrm == QueueReadMode::Blocking)
        {
            completion = eq.readBlocking(timeout);
        }
        else if constexpr (qrm == QueueReadMode::NonBlocking)
        {
            completion = eq.read();
        }
        else
        {
            static_assert(false, "Unsupported queue behaviour parameter");
        }

        return completion;
    }
}
