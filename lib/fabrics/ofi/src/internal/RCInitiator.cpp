// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RCInitiator.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <uuid.h>
#include <mxl-internal/Logging.hpp>
#include <rdma/fabric.h>
#include "mxl-internal/Flow.hpp"
#include "Domain.hpp"
#include "Exception.hpp"
#include "FabricInfo.hpp"
#include "GrainSlices.hpp"
#include "Protocol.hpp"
#include "Region.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{
    RCInitiatorEndpoint::RCInitiatorEndpoint(Endpoint ep, std::unique_ptr<EgressProtocol> proto, TargetInfo info)
        : _state(Idle{.ep = std::move(ep), .idleSince = std::chrono::steady_clock::time_point{}})
        , _info(std::move(info))
        , _proto(std::move(proto))
    {}

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
                [&](Connected const&)
                {
                    return _proto->hasPendingWork();
                }, // While connected, a target has pending work when there are transfers that have not yet completed.
                [](Flushing const&) { return true; }, // In the shutdown state, the target is waiting for a FI_SHUTDOWN event.
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

                    return Flushing{.ep = std::move(state.ep), .pending = pending};
                },
                [](Flushing state) -> State
                {
                    MXL_WARN("Another shutdown was requested while trying to flush the completion queue, ignoring.");
                    return state;
                },
                [](Done state) -> State { return state; },
            },
            std::move(_state));
    }

    void RCInitiatorEndpoint::terminate() noexcept
    {
        if (auto state = std::get_if<Flushing>(&_state); state)
        {
            MXL_INFO("Flushing complete, transitioning to done state.");
            _state = Done{};
        }
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
                        MXL_WARN("Received an error event in connected state, going idle. Error: {}", ev.error().toString());
                        return restart(state.ep);
                    }
                    else if (ev.isShutdown())
                    {
                        MXL_INFO("Remote endpoint has closed the connection");
                        return Flushing{.ep = std::move(state.ep), .pending = _proto->reset()};
                    }

                    return state;
                },
                [&](Flushing state) -> State
                {
                    if (ev.isShutdown())
                    {
                        MXL_INFO("Received a Shutdown Event while flushing the completion queue");
                    }
                    else if (ev.isError())
                    {
                        MXL_ERROR("Received an error while shutting down: {}", ev.error().toString());
                        return Done{};
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
        if (auto error = completion.tryErr(); error)
        {
            handleCompletionError(*error);
        }
        else if (auto data = completion.tryData(); data)
        {
            handleCompletionData(*data);
        }
    }

    void RCInitiatorEndpoint::transferGrain(std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t remotePayloadOffset,
        SliceRange const& sliceRange)
    {
        if (auto state = std::get_if<Connected>(&_state); state != nullptr)
        {
            _proto->transferGrain(state->ep, localIndex, remoteIndex, remotePayloadOffset, sliceRange);
        }
    }

    void RCInitiatorEndpoint::handleCompletionData(Completion::Data completion)
    {
        _state = std::visit(
            overloaded{[](Idle state) -> State
                {
                    MXL_WARN("Received a completion event while idle, ignoring.");
                    return state;
                },
                [](Connecting state) -> State
                {
                    MXL_WARN("Received a completion event while connecting, ignoring");
                    return state;
                },
                [&](Connected state) -> State
                {
                    _proto->processCompletion(completion);
                    return state;
                },
                [](Flushing state) -> State
                {
                    state.pending--; // Decrease the counter of completions that must still be flushed.
                    return state;
                },
                [](Done state) -> State
                {
                    MXL_DEBUG("Ignoring completion after shutdown");
                    return state;
                }},
            std::move(_state));
    }

    void RCInitiatorEndpoint::handleCompletionError(Completion::Error err)
    {
        MXL_ERROR("Received a completion error: {}", err.toString());
    }

    RCInitiatorEndpoint::Idle RCInitiatorEndpoint::restart(Endpoint const& old)
    {
        return Idle{.ep = Endpoint::create(old.domain(), old.id(), old.info()), .idleSince = std::chrono::steady_clock::now()};
    }

    std::unique_ptr<RCInitiator> RCInitiator::setup(mxlFabricsInitiatorConfig const& config)
    {
        auto provider = providerFromAPI(config.provider);
        if (!provider)
        {
            throw Exception::make(MXL_ERR_NO_FABRIC, "No provider available");
        }

        uint64_t caps = FI_RMA | FI_WRITE | FI_REMOTE_WRITE;
        caps |= config.deviceSupport ? FI_HMEM : 0;

        auto fabricInfoList = FabricInfoList::get(config.endpointAddress.node, config.endpointAddress.service, provider.value(), caps, FI_EP_MSG);

        if (fabricInfoList.begin() == fabricInfoList.end())
        {
            throw Exception::make(MXL_ERR_NO_FABRIC, "No suitable fabric available");
        }

        auto info = *fabricInfoList.begin();
        MXL_DEBUG("{}", fi_tostr(info.raw(), FI_TYPE_INFO));

        auto fabric = Fabric::open(info);
        auto domain = Domain::open(fabric);

        auto eq = EventQueue::open(fabric);
        auto cq = CompletionQueue::open(domain);

        auto regions = MxlRegions::fromAPI(config.regions);
        auto proto = selectEgressProtocol(regions->dataLayout(), regions->regions());
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

        _targets.emplace(id, RCInitiatorEndpoint{std::move(endpoint), std::move(proto), targetInfo});
    }

    void RCInitiator::removeTarget(TargetInfo const& targetInfo)
    {
        if (auto it = _targets.find(targetInfo.id); it != _targets.end())
        {
            it->second.shutdown();
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

    bool RCInitiator::hasPendingWork() const noexcept
    {
        // Check if any of the targets have pending work.
        for (auto& [_, target] : _targets)
        {
            if (target.hasPendingWork())
            {
                return true;
            }
        }

        return false;
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

    void RCInitiator::evictDeadEndpoints()
    {
        std::erase_if(_targets, [](auto const& item) { return item.second.canEvict(); });
    }

    void RCInitiator::blockOnCQ(std::chrono::steady_clock::duration timeout)
    {
        // A zero timeout would cause the queue to block indefinetly, which
        // is not our documented behaviour.
        if (timeout == std::chrono::milliseconds::zero())
        {
            // So just behave exactly like the non-blocking variant.
            makeProgress();
            return;
        }

        for (;;)
        {
            auto completion = _cq->readBlocking(timeout);
            if (!completion)
            {
                // No completion available, if we were flushing any endpoint, transition their state to done
                for (auto& [_, target] : _targets)
                {
                    target.terminate();
                }
                return;
            }

            // Find the endpoint that this completion was generated from
            auto ep = _targets.find(Endpoint::idFromToken(completion->token()));
            if (ep == _targets.end())
            {
                MXL_WARN("Received completion for an unknown endpoint");
            }

            return ep->second.consume(*completion);
        }
    }

    void RCInitiator::pollCQ()
    {
        for (;;)
        {
            auto completion = _cq->read();
            if (!completion)
            {
                // No completion available, if we were flushing any endpoint, transition their state to done
                for (auto& [_, target] : _targets)
                {
                    target.terminate();
                }
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

    bool RCInitiator::makeProgress()
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

        // Evict any peers that are dead and no longer will make progress.
        evictDeadEndpoints();

        return hasPendingWork();
    }

    bool RCInitiator::makeProgressBlocking(std::chrono::steady_clock::duration timeout)
    {
        // If the timeout is less than our maintainance interval, just check all the queues once, execute all maintainance tasks once
        // and block on the completion queue for the rest of the time.
        if (timeout < EQPollInterval)
        {
            makeProgress();
            blockOnCQ(timeout);
            return hasPendingWork();
        }

        auto deadline = std::chrono::steady_clock::now() + timeout;

        for (;;)
        {
            // If there is nothing more to do, return control to the caller.
            if (!hasPendingWork())
            {
                return false;
            }

            // Poll all queues, execute all maintainance actions
            makeProgress();

            // Calculate the remaining time until the user wants the blocking function to return. If there is no time left
            // (millisecond precision) return right away.
            auto timeUntilDeadline = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (timeUntilDeadline <= decltype(timeUntilDeadline){0})
            {
                return hasPendingWork();
            }

            // Block on the completion queue until a completion arrives, or the interval timeout occurrs.
            blockOnCQ(std::min(EQPollInterval, timeUntilDeadline));
        }

        return hasPendingWork();
    }

    void RCInitiator::shutdown()
    {
        for (auto& target : _targets)
        {
            target.second.shutdown();
        }
    }
}
