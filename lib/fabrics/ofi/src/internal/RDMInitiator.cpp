// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RDMInitiator.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
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
#include "Provider.hpp"
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
                    ep.addressVector()->remove(state.fiAddr);
                    return Done{};
                },
                [](Done state) -> State { return state; },
            },
            std::move(_state));
    }

    void RDMInitiatorTarget::transfer(Endpoint& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t remoteOffset,
        SliceRange const& sliceRange)
    {
        if (auto state = std::get_if<Activated>(&_state); state != nullptr)
        {
            _proto->transferGrain(ep, localIndex, remoteIndex, remoteOffset, sliceRange, state->fiAddr);
        }
    }

    bool RDMInitiatorTarget::hasPendingWork() const noexcept
    {
        return std::visit(
            overloaded{
                [](Idle const&) { return true; },
                [&](Activated const&) { return _proto->hasPendingWork(); },
                [](Done const&) { return false; },
            },
            _state);
    }

    void RDMInitiatorTarget::handleCompletion(Endpoint&, Completion completion)
    {
        if (completion.isErrEntry())
        {
            MXL_ERROR("Completion error.");
            return;
        }

        std::visit(
            overloaded{
                [](Idle const&) {},
                [&](Activated const&) { _proto->processCompletion(completion.data()); },
                [](Done const&) {},
            },
            _state);
    }

    std::unique_ptr<RDMInitiator> RDMInitiator::setup(mxlFabricsInitiatorConfig const& config)
    {
        auto provider = providerFromAPI(config.provider);
        if (!provider)
        {
            throw Exception::make(MXL_ERR_NO_FABRIC, "No provider available.");
        }

        auto caps = FI_WRITE;
        caps |= config.deviceSupport ? FI_HMEM : 0;

        auto fabricInfoList = FabricInfoList::get(config.endpointAddress.node, config.endpointAddress.service, provider.value(), caps, FI_EP_RDM);
        if (fabricInfoList.begin() == fabricInfoList.end())
        {
            throw Exception::make(MXL_ERR_NO_FABRIC, "No suitable fabric available");
        }

        auto info = *fabricInfoList.begin();
        MXL_DEBUG("{}", fi_tostr(info.raw(), FI_TYPE_INFO));

        auto fabric = Fabric::open(info);
        auto domain = Domain::open(fabric);

        auto endpoint = Endpoint::create(domain);

        auto cq = CompletionQueue::open(endpoint.domain());
        endpoint.bind(cq, FI_TRANSMIT | FI_RECV);

        auto av = AddressVector::open(endpoint.domain());
        endpoint.bind(av);

        endpoint.enable();

        auto regions = MxlRegions::fromAPI(config.regions);
        auto proto = selectEgressProtocol(regions->dataLayout(), regions->regions());

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
        : _endpoint(std::move(ep))
        , _proto(std::move(proto))
    {}

    void RDMInitiator::addTarget(TargetInfo const& targetInfo)
    {
        if (_remoteEndpoints.find(targetInfo.id) != _remoteEndpoints.end())
        {
            throw Exception::exists("A target with endpoint id {} has already been added to this initiator.", targetInfo.id);
        }

        auto token = Completion::randomToken();
        auto proto = _proto->createInstance(token, targetInfo);

        _remoteEndpoints.emplace(targetInfo.id, token);
        _targets.emplace(token, RDMInitiatorTarget(std::move(proto), targetInfo));
    }

    void RDMInitiator::removeTarget(TargetInfo const& targetInfo)
    {
        auto& remote = findRemoteByEndpoint(targetInfo.id);
        remote.shutdown(_endpoint);
        _targets.erase(_remoteEndpoints.extract(targetInfo.id).mapped());
    }

    void RDMInitiator::shutdown()
    {}

    void RDMInitiator::transferGrain(std::uint64_t grainIndex, std::uint16_t startSlice, std::uint16_t endSlice)
    {
        // Post a transfer work item to all targets. If the target is not in "Added" state
        // this is a no-op.
        for (auto& [_, target] : _targets)
        {
            // A completion will be posted to the completion queue, after which the counter will be decremented again
            target.transfer(_endpoint, grainIndex, grainIndex, MXL_GRAIN_PAYLOAD_OFFSET, SliceRange::make(startSlice, endSlice));
        }
    }

    void RDMInitiator::transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t payloadOffset,
        std::uint16_t startSlice, std::uint16_t endSlice)
    {
        // A completion will be posted to the completion queue per transfer, after which the counter will be decremented again
        findRemoteByEndpoint(targetId).transfer(_endpoint, localIndex, remoteIndex, payloadOffset, SliceRange::make(startSlice, endSlice));
    }

    // makeProgress
    bool RDMInitiator::makeProgress()
    {
        activateIdleEndpoints();
        pollCQ();
        return hasPendingWork();
    }

    // makeProgressBlocking
    bool RDMInitiator::makeProgressBlocking(std::chrono::steady_clock::duration timeout)
    {
        auto now = std::chrono::steady_clock::now();
        activateIdleEndpoints();
        auto elapsed = std::chrono::steady_clock::now() - now;

        auto remaining = timeout - elapsed;
        if (remaining.count() >= 0)
        {
            blockOnCQ(remaining);
        }
        else
        {
            pollCQ();
        }

        return hasPendingWork();
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
            throw Exception::invalidState("No target found for completion token value");
        }

        return it->second;
    }

    bool RDMInitiator::hasPendingWork() const noexcept
    {
        for (auto const& [_, remote] : _targets)
        {
            if (remote.hasPendingWork())
            {
                return true;
            }
        }

        return false;
    }

    void RDMInitiator::blockOnCQ(std::chrono::system_clock::duration timeout)
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
        }
    }

    void RDMInitiator::pollCQ()
    {
        if (auto completion = _endpoint.completionQueue()->read(); completion)
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
