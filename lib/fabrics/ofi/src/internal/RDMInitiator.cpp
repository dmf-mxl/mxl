// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RDMInitiator.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <variant>
#include <mxl-internal/Logging.hpp>
#include <rdma/fabric.h>
#include <rdma/fi_eq.h>
#include "mxl-internal/Flow.hpp"
#include "mxl/fabrics.h"
#include "mxl/mxl.h"
#include "AddressVector.hpp"
#include "CompletionQueue.hpp"
#include "Endpoint.hpp"
#include "Exception.hpp"
#include "Fabric.hpp"
#include "FabricInfoHelpers.hpp"
#include "Region.hpp"
#include "TargetInfo.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{
    RDMInitiatorTarget::RDMInitiatorTarget(std::unique_ptr<EgressProtocol> proto, TargetInfo remoteInfo)
        : _state(Idle{})
        , _proto(std::move(proto))
        , _remoteInfo(std::move(remoteInfo))
    {}

    bool RDMInitiatorTarget::isIdle() const noexcept
    {
        return std::holds_alternative<Idle>(_state);
    }

    void RDMInitiatorTarget::activate(Endpoint& ep)
    {
        _state = std::visit(
            overloaded{
                [&](Idle) -> State
                {
                    auto fiAddr = ep.addressVector()->insert(_remoteInfo.fabricAddress);
                    return Activated{.fiAddr = fiAddr};
                },
                [](Activated state) -> State { return state; },
                [](Removing state) -> State { return state; },
                [](Done) -> State { throw Exception::invalidState("Endpoint has been shutdown and can no longer be used."); },
            },
            std::move(_state));
    }

    void RDMInitiatorTarget::shutdown(Endpoint& ep)
    {
        _state = std::visit(
            overloaded{
                [](Idle) -> State
                {
                    MXL_WARN("Shutdown requested while waiting to activate, aborting.");
                    return Done{};
                },
                [&](Activated state) -> State
                {
                    if (_proto->hasPendingWork())
                    {
                        MXL_INFO("Target removed that had outstanding writes, transitioning to removing state.");
                        return Removing{.fiAddr = state.fiAddr, .deadline = std::chrono::steady_clock::now() + RemoveTimeout};
                    }

                    ep.addressVector()->remove(state.fiAddr);
                    return Done{};
                },
                [](Removing state) -> State { return state; },
                [](Done state) -> State { return state; },
            },
            std::move(_state));
    }

    void RDMInitiatorTarget::finishRemoval(Endpoint& ep)
    {
        auto const state = std::get_if<Removing>(&_state);
        if (state == nullptr)
        {
            return;
        }

        if (std::chrono::steady_clock::now() >= state->deadline)
        {
            MXL_WARN("Writes to the removed target did not complete within {} ms, removing it anyway.",
                std::chrono::duration_cast<std::chrono::milliseconds>(RemoveTimeout).count());
        }
        else
        {
            return;
        }

        ep.addressVector()->remove(state->fiAddr);
        _state = Done{};
    }

    bool RDMInitiatorTarget::canEvict() const noexcept
    {
        return std::holds_alternative<Done>(_state);
    }

    void RDMInitiatorTarget::transferGrain(Endpoint const& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t remotePayloadOffset,
        SliceRange const& sliceRange)
    {
        if (auto const state = std::get_if<Activated>(&_state); state != nullptr)
        {
            _proto->transferGrain(ep, localIndex, remoteIndex, remotePayloadOffset, sliceRange, state->fiAddr);
        }
    }

    void RDMInitiatorTarget::transferSamples(Endpoint const& ep, std::uint64_t headIndex, std::size_t count)
    {
        if (auto const state = std::get_if<Activated>(&_state); state != nullptr)
        {
            _proto->transferSamples(ep, headIndex, count, state->fiAddr);
        }
    }

    bool RDMInitiatorTarget::hasPendingWork() const noexcept
    {
        return std::visit(
            overloaded{
                [](Idle const&) { return true; },
                [&](Activated const&) { return _proto->hasPendingWork(); },
                [](Removing const&) { return true; },
                [](Done const&) { return false; },
            },
            _state);
    }

    void RDMInitiatorTarget::handleCompletion(Endpoint&, Completion completion)
    {
        if (auto error = completion.tryErr(); error)
        {
            MXL_ERROR("Received a completion error: {}", error->toString());
        }

        std::visit(
            overloaded{
                [](Idle const&) {},
                [&](Activated const&) { _proto->processCompletion(completion); },
                [&](Removing const&) { _proto->processCompletion(completion); },
                [](Done const&) {},
            },
            _state);
    }

    std::unique_ptr<RDMInitiator> RDMInitiator::setup(mxlFabricsInitiatorConfig const& config, FabricInfoView info,
        InitiatorSetupOptions const& options)
    {
        requireCapability(info, FI_WRITE, "Interface is missing required remote write capability");

        auto cqAttr = CompletionQueue::Attributes::defaults();
        if (options.cqDepth)
        {
            cqAttr.size = *options.cqDepth;
        }
        if (config.interface.provider == MXL_FABRICS_PROVIDER_EFA)
        {
            if (!CompletionQueue::isWaitObjectSupportedForEFA())
            {
                if ((config.interface.caps.flags & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS) != 0)
                {
                    throw Exception::make(MXL_ERR_NO_FABRIC, "Blocking API support requested, but not available for this fabric/version");
                }

                cqAttr.waitObject = FI_WAIT_NONE;
            }
        }

        auto fabric = Fabric::open(info);
        auto domain = Domain::open(fabric);
        auto endpoint = Endpoint::create(domain);
        auto cq = CompletionQueue::open(endpoint.domain(), cqAttr);
        endpoint.bind(cq, FI_TRANSMIT | FI_RECV);

        auto av = AddressVector::open(endpoint.domain());
        endpoint.bind(av);

        endpoint.enable();

        auto regions = MxlRegions::forReader(config.reader);
        auto proto = selectEgressProtocol(regions.dataLayout(), regions.regions());

        proto->registerMemory(domain);

        struct MakeUniqueEnabler : RDMInitiator
        {
            MakeUniqueEnabler(Endpoint ep, std::unique_ptr<EgressProtocolTemplate> proto)
                : RDMInitiator(std::move(ep), std::move(proto))
            {}
        };

        return std::make_unique<MakeUniqueEnabler>(std::move(endpoint), std::move(proto));
    }

    RDMInitiator::RDMInitiator(Endpoint ep, std::unique_ptr<EgressProtocolTemplate> proto)
        : _proto(std::move(proto))
        , _endpoint(std::move(ep))
    {}

    void RDMInitiator::addTarget(TargetInfo const& targetInfo)
    {
        if (_remoteEndpoints.contains(targetInfo.id))
        {
            throw Exception::exists("A target with endpoint id {} has already been added to this initiator.", targetInfo.id);
        }

        auto const token = Completion::randomToken();
        auto proto = _proto->createInstance(token, targetInfo);
        proto->registerMemory(_endpoint.domain());

        _remoteEndpoints.emplace(targetInfo.id, token);
        _targets.emplace(token, RDMInitiatorTarget(std::move(proto), targetInfo));
    }

    void RDMInitiator::removeTarget(TargetInfo const& targetInfo)
    {
        auto& remote = findRemoteByEndpoint(targetInfo.id);
        remote.shutdown(_endpoint);

        // The target can no longer be found by its id, so no new transfers are posted to it. It stays in _targets until its outstanding
        // writes have completed, so that their completions can be counted and the protocol memory stays valid.
        auto const token = _remoteEndpoints.extract(targetInfo.id).mapped();
        if (remote.canEvict())
        {
            _targets.erase(token);
        }
    }

    void RDMInitiator::shutdown()
    {
        for (auto& [_, target] : _targets)
        {
            target.shutdown(_endpoint);
        }
        _remoteEndpoints.clear();

        auto const deadline = std::chrono::steady_clock::now() + ShutdownTimeout;
        for (;;)
        {
            evictRemovedTargets();

            if (_targets.empty())
            {
                return;
            }

            if (std::chrono::steady_clock::now() >= deadline)
            {
                MXL_WARN("{} targets were not removed in time.", _targets.size());
                return;
            }

            std::this_thread::sleep_for(ShutdownPollInterval);
            pollCQ();
        }
    }

    void RDMInitiator::transferGrain(std::uint64_t grainIndex, std::uint16_t startSlice, std::uint16_t endSlice)
    {
        // Post a transfer work item to all targets. If the target is not in "Added" state
        // this is a no-op.
        for (auto& [_, target] : _targets)
        {
            // A completion will be posted to the completion queue, after which the counter will be decremented again
            target.transferGrain(_endpoint, grainIndex, grainIndex, MXL_GRAIN_PAYLOAD_OFFSET, SliceRange::make(startSlice, endSlice));
        }
    }

    void RDMInitiator::transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t payloadOffset,
        std::uint16_t startSlice, std::uint16_t endSlice)
    {
        // A completion will be posted to the completion queue per transfer, after which the counter will be decremented again
        findRemoteByEndpoint(targetId).transferGrain(_endpoint, localIndex, remoteIndex, payloadOffset, SliceRange::make(startSlice, endSlice));
    }

    void RDMInitiator::transferSamples(std::uint64_t headIndex, std::size_t count)
    {
        for (auto& [_, target] : _targets)
        {
            target.transferSamples(_endpoint, headIndex, count);
        }
    }

    // makeProgress
    Initiator::MakeProgressResult RDMInitiator::makeProgress()
    {
        activateIdleEndpoints();
        pollCQ();
        evictRemovedTargets();
        return afterProgressResult();
    }

    // makeProgressBlocking
    Initiator::MakeProgressResult RDMInitiator::makeProgressBlocking(std::chrono::steady_clock::duration timeout)
    {
        auto now = std::chrono::steady_clock::now();
        activateIdleEndpoints();
        auto elapsed = std::chrono::steady_clock::now() - now;

        auto remaining = timeout - elapsed;
        if (remaining.count() >= 0)
        {
            try
            {
                blockOnCQ(remaining);
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
        else
        {
            pollCQ();
        }

        evictRemovedTargets();
        return afterProgressResult();
    }

    RDMInitiatorTarget& RDMInitiator::findRemoteByEndpoint(Endpoint::Id id)
    {
        auto it = _remoteEndpoints.find(id);
        if (it == _remoteEndpoints.end())
        {
            throw Exception::notFound("No target found for endpoint id {}", id);
        }

        return findRemoteByToken(it->second);
    }

    RDMInitiatorTarget& RDMInitiator::findRemoteByToken(Completion::Token token)
    {
        auto it = _targets.find(token);
        if (it == _targets.end())
        {
            throw Exception::notFound("No target found for completion token value");
        }

        return it->second;
    }

    Initiator::MakeProgressResult RDMInitiator::afterProgressResult() const noexcept
    {
        for (auto const& [_, remote] : _targets)
        {
            if (remote.hasPendingWork())
            {
                return Initiator::NotReady{};
            }
        }

        return Initiator::Ready{};
    }

    void RDMInitiator::blockOnCQ(std::chrono::steady_clock::duration timeout)
    {
        // A zero timeout would cause the queue to block indefinetly, which
        // is not our documented behaviour.
        if (timeout == std::chrono::milliseconds::zero())
        {
            // So just behave exactly like the non-blocking variant.
            pollCQ();
            return;
        }

        if (auto completion = _endpoint.completionQueue()->readBlocking(timeout); completion)
        {
            processCompletion(*completion);
            pollCQ();
        }
    }

    void RDMInitiator::pollCQ()
    {
        while (auto completion = _endpoint.completionQueue()->read())
        {
            processCompletion(*completion);
        }
    }

    void RDMInitiator::activateIdleEndpoints()
    {
        for (auto& [_, target] : _targets)
        {
            target.activate(_endpoint);
        }
    }

    void RDMInitiator::evictRemovedTargets()
    {
        for (auto& [_, target] : _targets)
        {
            target.finishRemoval(_endpoint);
        }

        std::erase_if(_targets, [](auto const& item) { return item.second.canEvict(); });
    }

    void RDMInitiator::processCompletion(Completion completion)
    {
        auto it = _targets.find(completion.token());
        if (it == _targets.end())
        {
            MXL_ERROR("Dropping completion for unknown target.");
            return;
        }

        it->second.handleCompletion(_endpoint, completion);
    }

} // namespace mxl::lib::fabrics::ofi
