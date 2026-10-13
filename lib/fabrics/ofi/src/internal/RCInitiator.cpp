// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RCInitiator.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <ranges>
#include <uuid.h>
#include <mxl-internal/Logging.hpp>
#include <rdma/fabric.h>
#include "mxl-internal/Flow.hpp"
#include "Domain.hpp"
#include "Exception.hpp"
#include "FabricInfo.hpp"
#include "FabricInfoHelpers.hpp"
#include "Protocol.hpp"
#include "Region.hpp"
#include "SliceRange.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{
    RCInitiatorEndpoint::RCInitiatorEndpoint(Endpoint ep, std::unique_ptr<EgressProtocol> proto, TargetInfo info)
        : _state(Idle{.ep = std::move(ep), .idleSince = std::chrono::steady_clock::time_point{}})
        , _info(std::move(info))
        , _proto(std::move(proto))
    {}

    TargetInfo const& RCInitiatorEndpoint::info() const noexcept
    {
        return _info;
    }

    bool RCInitiatorEndpoint::isIdle() const noexcept
    {
        return std::holds_alternative<Idle>(_state);
    }

    bool RCInitiatorEndpoint::canEvict() const noexcept
    {
        return std::holds_alternative<Done>(_state);
    }

    bool RCInitiatorEndpoint::hasPendingWork() const noexcept
    {
        return std::visit(
            overloaded{
                [](std::monostate) { return false; },   // Something went wrong with this target, but there is probably no work to do.
                [](Idle const&) { return true; },       // An idle target means there is no work to do right now.
                [](Connecting const&) { return true; }, // In the connecting state, the target is waiting for a connected event.
                [&](Connected const&) { return _proto->hasPendingWork(); }, // While connected, a target has pending work when
                                                                            // there are transfers that have not yet completed.
                [](Flushing const&) { return true; }, // While flushing, the target waits for a shutdown event and outstanding completions.
                [](Done const&) { return false; },    // In the done state, there is no pending work.
            },
            _state);
    }

    void RCInitiatorEndpoint::shutdown()
    {
        _state = std::visit(
            overloaded{
                [](Idle) -> State
                {
                    MXL_INFO("Shutdown requested while waiting to activate, aborting.");
                    return Done{};
                },
                [](Connecting) -> State
                {
                    MXL_INFO("Shutdown requested while trying to connect, aborting.");
                    return Done{};
                },
                [&](Connected state) -> State
                {
                    MXL_INFO("Shutting down");
                    auto pending = _proto->reset();
                    state.ep.shutdown();

                    return startFlush(std::move(state.ep), pending, false, false);
                },
                [](Flushing state) -> State
                {
                    if (state.reconnect)
                    {
                        MXL_INFO("Shutdown requested while flushing after a connection error, the endpoint will not reconnect.");
                        state.reconnect = false;
                    }
                    else
                    {
                        MXL_WARN("Another shutdown was requested while trying to flush the completion queue, ignoring.");
                    }
                    return state;
                },
                [](Done state) -> State { return state; },
            },
            std::move(_state));
    }

    void RCInitiatorEndpoint::finishFlush()
    {
        auto const state = std::get_if<Flushing>(&_state);
        if (state == nullptr)
        {
            return;
        }

        if (state->shutdownReceived && (state->pending == 0))
        {
            MXL_INFO("Flushing complete, transitioning to {} state.", state->reconnect ? "idle" : "done");
        }
        else if (std::chrono::steady_clock::now() >= state->deadline)
        {
            MXL_WARN("Flushing timed out with {} outstanding operations, shutdown event received: {}. Transitioning to {} state.",
                state->pending,
                state->shutdownReceived,
                state->reconnect ? "idle" : "done");
        }
        else
        {
            return;
        }

        if (state->reconnect)
        {
            _state = restart(state->ep);
        }
        else
        {
            _state = Done{};
        }
    }

    RCInitiatorEndpoint::Flushing RCInitiatorEndpoint::startFlush(Endpoint ep, std::size_t pending, bool shutdownReceived, bool reconnect)
    {
        return Flushing{.ep = std::move(ep),
            .pending = pending,
            .shutdownReceived = shutdownReceived,
            .reconnect = reconnect,
            .deadline = std::chrono::steady_clock::now() + FlushTimeout};
    }

    void RCInitiatorEndpoint::activate(std::shared_ptr<CompletionQueue> const& cq, std::shared_ptr<EventQueue> const& eq)
    {
        _state = std::visit(
            overloaded{
                [&](Idle state) -> State
                {
                    // If the target is in the idle state for more than 5 seconds, it will be restarted.
                    auto idleDuration = std::chrono::steady_clock::now() - state.idleSince;
                    if (idleDuration < std::chrono::seconds(5))
                    {
                        return state;
                    }

                    MXL_INFO("Endpoint has been idle for {}ms, activating",
                        std::chrono::duration_cast<std::chrono::milliseconds>(idleDuration).count());

                    // The endpoint in an idle target is always fresh and thus needs to be bound the the queues.
                    state.ep.bind(eq);
                    state.ep.bind(cq, FI_TRANSMIT);

                    // Transition into the connecting state
                    auto const faddr = _info.fabricAddress.decode();
                    MXL_INFO("Connecting to {}", faddr.toString());
                    state.ep.connect(_info.fabricAddress);
                    return Connecting{.ep = std::move(state.ep)};
                },
                [](Connecting state) -> State { return state; },
                [](Connected state) -> State { return state; },
                [](Flushing state) -> State { return state; },
                [](Done state) -> State { return state; },
            },
            std::move(_state));
    }

    void RCInitiatorEndpoint::consume(Event ev)
    {
        _state = std::visit(
            overloaded{
                [](Idle state) -> State { return state; }, // Nothing to do when idle.
                [&](Connecting state) -> State
                {
                    if (ev.isError())
                    {
                        MXL_ERROR("Failed to connect endpoint: {}", ev.error().toString());

                        // Go into the idle state with a new endpoint
                        return restart(state.ep);
                    }
                    else if (ev.isConnected())
                    {
                        MXL_INFO("Endpoint is now connected");
                        return Connected{.ep = std::move(state.ep)};
                    }
                    else if (ev.isShutdown())
                    {
                        MXL_WARN("Received a shutdown event while connecting, going idle");

                        // Go to idle state with a new endpoint
                        return restart(state.ep);
                    }

                    MXL_WARN("Received an unexpected event while establishing a connection");
                    return state;
                },
                [&](Connected state) -> State
                {
                    if (ev.isError())
                    {
                        MXL_WARN("Received an error event in connected state, going idle after flushing. Error: {}", ev.error().toString());
                        return startFlush(std::move(state.ep), _proto->reset(), true, true);
                    }
                    else if (ev.isShutdown())
                    {
                        MXL_INFO("Remote endpoint has closed the connection");
                        return startFlush(std::move(state.ep), _proto->reset(), true, false);
                    }

                    return state;
                },
                [&](Flushing state) -> State
                {
                    if (ev.isShutdown())
                    {
                        MXL_INFO("Received a shutdown event while flushing the completion queue");
                        state.shutdownReceived = true;
                    }
                    else if (ev.isError())
                    {
                        // The connection is gone and no shutdown event will follow. Outstanding operations can still complete.
                        MXL_ERROR("Received an error while shutting down: {}", ev.error().toString());
                        state.shutdownReceived = true;
                    }
                    else
                    {
                        MXL_ERROR("Received an unexpected event while shutting down");
                    }
                    return state;
                },
                [](Done state) -> State { return state; },
            },
            std::move(_state));
    }

    void RCInitiatorEndpoint::consume(Completion completion)
    {
        auto const error = completion.tryErr();

        std::visit(
            overloaded{
                [](Idle const&) { MXL_WARN("Received a completion event while idle, ignoring."); },
                [](Connecting const&) { MXL_WARN("Received a completion event while connecting, ignoring."); },
                [&](Connected const&)
                {
                    // A failed operation is finished as well, so it is counted like a successful one.
                    if (error)
                    {
                        MXL_ERROR("Received a completion error: {}", error->toString());
                    }
                    _proto->processCompletion(completion);
                },
                [&](Flushing& state)
                {
                    // Operations that were still queued when the connection was shut down complete with an error. They count as flushed.
                    if (error)
                    {
                        MXL_DEBUG("Received a completion error while flushing: {}", error->toString());
                    }
                    if (state.pending > 0)
                    {
                        --state.pending;
                    }
                },
                [](Done const&) { MXL_DEBUG("Ignoring completion after shutdown."); },
            },
            _state);
    }

    void RCInitiatorEndpoint::transferGrain(std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t remotePayloadOffset,
        SliceRange const& sliceRange)
    {
        if (auto const state = std::get_if<Connected>(&_state); state != nullptr)
        {
            _proto->transferGrain(state->ep, localIndex, remoteIndex, remotePayloadOffset, sliceRange);
        }
    }

    void RCInitiatorEndpoint::transferSamples(std::uint64_t headIndex, std::size_t count)
    {
        if (auto const state = std::get_if<Connected>(&_state); state != nullptr)
        {
            _proto->transferSamples(state->ep, headIndex, count);
        }
    }

    RCInitiatorEndpoint::Idle RCInitiatorEndpoint::restart(Endpoint const& old)
    {
        return Idle{.ep = Endpoint::create(old.domain(), old.id(), old.info()), .idleSince = std::chrono::steady_clock::now()};
    }

    std::unique_ptr<RCInitiator> RCInitiator::setup(mxlFabricsInitiatorConfig const& config, FabricInfoView info,
        InitiatorSetupOptions const& options)
    {
        requireCapability(info, FI_WRITE, "Interface is missing required write capability");
        MXL_DEBUG("{}", fi_tostr(info.raw(), FI_TYPE_INFO));

        auto fabric = Fabric::open(info);
        auto domain = Domain::open(fabric);

        auto eq = EventQueue::open(fabric);
        auto cqAttr = CompletionQueue::Attributes::defaults();
        if (options.cqDepth)
        {
            cqAttr.size = *options.cqDepth;
        }
        auto cq = CompletionQueue::open(domain, cqAttr);

        auto regions = MxlRegions::forReader(config.reader);
        auto proto = selectEgressProtocol(regions.dataLayout(), regions.regions());
        proto->registerMemory(domain);

        struct MakeUniqueEnabler : RCInitiator
        {
            MakeUniqueEnabler(std::shared_ptr<Domain> domain, std::shared_ptr<CompletionQueue> cq, std::shared_ptr<EventQueue> eq,
                std::unique_ptr<EgressProtocolTemplate> proto)
                : RCInitiator(std::move(domain), std::move(cq), std::move(eq), std::move(proto))
            {}
        };

        return std::make_unique<MakeUniqueEnabler>(std::move(domain), std::move(cq), std::move(eq), std::move(proto));
    }

    RCInitiator::RCInitiator(std::shared_ptr<Domain> domain, std::shared_ptr<CompletionQueue> cq, std::shared_ptr<EventQueue> eq,
        std::unique_ptr<EgressProtocolTemplate> proto)
        : _domain(std::move(domain))
        , _cq(std::move(cq))
        , _eq(std::move(eq))
        , _proto(std::move(proto))
    {}

    void RCInitiator::addTarget(TargetInfo const& targetInfo)
    {
        auto endpoint = Endpoint::create(_domain);
        auto id = endpoint.id();
        auto proto = _proto->createInstance(Endpoint::tokenFromId(id), targetInfo);
        proto->registerMemory(_domain);

        _targets.emplace(id, RCInitiatorEndpoint{std::move(endpoint), std::move(proto), targetInfo});
    }

    void RCInitiator::removeTarget(TargetInfo const& targetInfo)
    {
        if (auto target = std::ranges::find_if(_targets, [&targetInfo](auto const& item) { return item.second.info().id == targetInfo.id; });
            target != _targets.end())
        {
            target->second.shutdown();
        }
        else
        {
            throw Exception::notFound("Target with id {} not found", targetInfo.id);
        }
    }

    void RCInitiator::transferGrain(std::uint64_t grainIndex, std::uint16_t startSlice, std::uint16_t endSlice)
    {
        // Post a transfer work item to all targets. If the target is not in a connected state
        // this is a no-op.
        for (auto& [_, target] : _targets)
        {
            target.transferGrain(grainIndex, grainIndex, MXL_GRAIN_PAYLOAD_OFFSET, SliceRange::make(startSlice, endSlice));
        }
    }

    void RCInitiator::transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t payloadOffset,
        std::uint16_t startSlice, std::uint16_t endSlice)
    {
        auto it = _targets.find(targetId);
        if (it == _targets.end())
        {
            throw Exception::notFound("Target with id {} not found", targetId);
        }

        it->second.transferGrain(localIndex, remoteIndex, payloadOffset, SliceRange::make(startSlice, endSlice));
    }

    void RCInitiator::transferSamples(std::uint64_t headIndex, std::size_t count)
    {
        for (auto& [_, target] : _targets)
        {
            target.transferSamples(headIndex, count);
        }
    }

    Initiator::MakeProgressResult RCInitiator::afterProgressResult() const noexcept
    {
        // Check if any of the targets have pending work.
        for (auto& [_, target] : _targets)
        {
            if (target.hasPendingWork())
            {
                return Initiator::NotReady{};
            }
        }

        return Initiator::Ready{};
    }

    bool RCInitiator::hasTarget() const noexcept
    {
        return _targets.size() > 0;
    }

    void RCInitiator::activateIdleEndpoints()
    {
        // Call the activate function on all endpoints. This is a no-op when the endpoint is not idle
        // and there is probably not that many of them.
        for (auto& [_, target] : _targets)
        {
            target.activate(_cq, _eq);
        }
    }

    void RCInitiator::finishFlushingEndpoints()
    {
        for (auto& [_, target] : _targets)
        {
            target.finishFlush();
        }
    }

    void RCInitiator::evictDeadEndpoints()
    {
        std::erase_if(_targets, [](auto const& item) { return item.second.canEvict(); });
    }

    Initiator::MakeProgressResult RCInitiator::blockOnCQ(std::chrono::steady_clock::duration timeout)
    {
        // A zero timeout would cause the queue to block indefinetly, which
        // is not our documented behaviour.
        if (timeout == std::chrono::milliseconds::zero())
        {
            // So just behave exactly like the non-blocking variant.
            makeProgress();
            return afterProgressResult();
        }

        for (;;)
        {
            try
            {
                auto completion = _cq->readBlocking(timeout);
                if (!completion)
                {
                    return afterProgressResult();
                }

                // Find the endpoint that this completion was generated from
                auto ep = _targets.find(Endpoint::idFromToken(completion->token()));
                if (ep == _targets.end())
                {
                    // The endpoint can already be evicted, for example after a flush timeout.
                    MXL_WARN("Received completion for an unknown endpoint");

                    return afterProgressResult();
                }

                ep->second.consume(*completion);

                return afterProgressResult();
            }
            catch (FabricException const& ex)
            {
                if (ex.isInterrupted())
                {
                    return Initiator::Interrupted{};
                }

                throw;
            }
        }
    }

    void RCInitiator::pollCQ()
    {
        for (;;)
        {
            auto completion = _cq->read();
            if (!completion)
            {
                break;
            }

            // Find the endpoint this completion was generated from.
            auto ep = _targets.find(Endpoint::idFromToken(completion->token()));
            if (ep == _targets.end())
            {
                MXL_WARN("Received completion for an unknown endpoint");

                continue;
            }

            ep->second.consume(*completion);
        }
    }

    void RCInitiator::pollEQ()
    {
        for (;;)
        {
            auto event = _eq->read();
            if (!event)
            {
                break;
            }

            // Find the endpoint this event was generated from.
            auto ep = _targets.find(Endpoint::idFromFID(event->fid()));
            if (ep == _targets.end())
            {
                MXL_WARN("Received event for an unknown endpoint");

                continue;
            }

            ep->second.consume(*event);
        }
    }

    Initiator::MakeProgressResult RCInitiator::makeProgress()
    {
        if (!hasTarget())
        {
            throw Exception::interrupted("No more targets available while calling makeProgress.");
        }

        // Activate any peers that might be idle and waiting for activation.
        activateIdleEndpoints();

        // Poll the completion and event queue once and process pending events.
        pollCQ();
        pollEQ();

        // Complete the shutdown of endpoints that have received all outstanding completions and the shutdown event.
        finishFlushingEndpoints();

        // Evict any peers that are dead and no longer will make progress.
        evictDeadEndpoints();

        return afterProgressResult();
    }

    Initiator::MakeProgressResult RCInitiator::makeProgressBlocking(std::chrono::steady_clock::duration timeout)
    {
        // If the timeout is less than our maintainance interval, just check all the queues once, execute all maintainance tasks once
        // and block on the completion queue for the rest of the time.
        if (timeout < EQPollInterval)
        {
            makeProgress();
            return blockOnCQ(timeout);
        }

        auto deadline = std::chrono::steady_clock::now() + timeout;

        for (;;)
        {
            // Poll all queues, execute all maintainance actions
            if (std::holds_alternative<Initiator::Ready>(makeProgress()))
            {
                return Initiator::Ready{};
            }

            // Calculate the remaining time until the user wants the blocking function to return. If there is no time left
            // (millisecond precision) return right away.
            auto timeUntilDeadline = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (timeUntilDeadline <= decltype(timeUntilDeadline){0})
            {
                return afterProgressResult();
            }

            // Block on the completion queue until a completion arrives, or the interval timeout occurs.
            auto const res = blockOnCQ(std::min(EQPollInterval, timeUntilDeadline));
            if (std::holds_alternative<Initiator::NotReady>(res))
            {
                continue;
            }

            return res;
        }
    }

    void RCInitiator::shutdown()
    {
        for (auto& target : _targets)
        {
            target.second.shutdown();
        }
    }
}
