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
#include "mxl/fabrics.h"
#include "mxl/mxl.h"
#include "Address.hpp"
#include "AddressVector.hpp"
#include "CompletionQueue.hpp"
#include "Endpoint.hpp"
#include "Exception.hpp"
#include "Fabric.hpp"
#include "ImmData.hpp"
#include "Provider.hpp"
#include "Region.hpp"
#include "TargetInfo.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{
    RDMInitiatorEndpoint::RDMInitiatorEndpoint(std::shared_ptr<Endpoint> ep, FabricAddress remote, std::vector<RemoteRegion> remoteRegions)
        : _state(Idle{})
        , _ep(std::move(ep))
        , _addr(std::move(remote))
        , _regions(std::move(remoteRegions))
    {}

    bool RDMInitiatorEndpoint::isIdle() const noexcept
    {
        return std::holds_alternative<Idle>(_state);
    }

    bool RDMInitiatorEndpoint::canEvict() const noexcept
    {
        return std::holds_alternative<Done>(_state);
    }

    void RDMInitiatorEndpoint::activate()
    {
        _state = std::visit(
            overloaded{
                [&](Idle) -> State
                {
                    auto fiAddr = _ep->addressVector()->insert(_addr);
                    return Activated{.fiAddr = fiAddr};
                },
                [](Activated state) -> State { return state; },
                [](Done state) -> State { return state; },
            },
            std::move(_state));
    }

    void RDMInitiatorEndpoint::shutdown()
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
                    _ep->addressVector()->remove(state.fiAddr);
                    return Done{};
                },
                [](Done state) -> State { return state; },
            },
            std::move(_state));
    }

    void RDMInitiatorEndpoint::postTransferWithRemoteIndex(LocalRegion const& localRegion, std::uint64_t remoteIndex, std::uint64_t remoteOffset,
        std::uint32_t size, std::uint16_t validSlices)
    {
        assert(remoteIndex < _regions.size());

        if (auto state = std::get_if<Activated>(&_state); state != nullptr)
        {
            // Find the remote region to which this grain should be written.
            auto const& remoteRegion = _regions[remoteIndex].sub(remoteOffset, size);

            // Post a write work item to the endpoint and increment the pending counter. When the write is complete,
            _ep->write(localRegion, remoteRegion, state->fiAddr, ImmDataDiscrete{remoteIndex, validSlices}.data()); // TODO: handle sliceIndex
        }
    }

    void RDMInitiatorEndpoint::postTransferWithGrainIndex(LocalRegion const& localRegion, std::uint64_t grainIndex, std::uint64_t remoteOffset,
        std::uint32_t size, std::uint16_t validSlices)
    {
        return postTransferWithRemoteIndex(localRegion, grainIndex % _regions.size(), remoteOffset, size, validSlices);
    }

    std::unique_ptr<RDMInitiator> RDMInitiator::setup(mxlInitiatorConfig const& config)
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

        auto mxlRegions = MxlRegions::fromAPI(config.regions);

        if (mxlRegions && !mxlRegions->regions().empty())
        {
            domain->registerRegions(mxlRegions->regions(), FI_WRITE);
        }

        auto endpoint = std::make_shared<Endpoint>(Endpoint::create(std::move(domain)));

        auto cq = CompletionQueue::open(endpoint->domain());
        endpoint->bind(cq, FI_TRANSMIT | FI_RECV);

        auto av = AddressVector::open(endpoint->domain());
        endpoint->bind(av);

        endpoint->enable();

        struct MakeUniqueEnabler : RDMInitiator
        {
            MakeUniqueEnabler(std::shared_ptr<Endpoint> ep)
                : RDMInitiator(std::move(ep))
            {}
        };

        return std::make_unique<MakeUniqueEnabler>(std::move(endpoint));
    }

    void RDMInitiator::addTarget(TargetInfo const& targetInfo)
    {
        _targets.emplace(targetInfo.id, RDMInitiatorEndpoint(_endpoint, targetInfo.fabricAddress, targetInfo.remoteRegions));
    }

    void RDMInitiator::removeTarget(TargetInfo const& targetInfo)
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

    void RDMInitiator::transferGrain(std::uint64_t grainIndex, std::uint64_t offset, std::uint32_t size, std::uint16_t validSlices)
    {
        // Find the local region in which the grain with this index is stored.
        auto localRegion = _localRegions[grainIndex % _localRegions.size()].sub(offset, size);

        // Post a transfer work item to all targets. If the target is not in "Added" state
        // this is a no-op.
        for (auto& [_, target] : _targets)
        {
            target.postTransferWithGrainIndex(localRegion, grainIndex, offset, size, validSlices);

            // A completion will be posted to the completion queue, after which the counter will be decremented again
            pending++;
        }
    }

    void RDMInitiator::transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t localOffset, std::uint64_t remoteIndex,
        std::uint64_t remoteOffset, std::uint32_t size, std::uint16_t validSlices)
    {
        assert(localIndex < _localRegions.size());

        // Find the local region in which the grain with this index is stored.
        auto localRegion = _localRegions[localIndex].sub(localOffset, size);

        // If the target is not in "Added" state this is a no-op.
        auto it = _targets.find(targetId);
        if (it != _targets.end())
        {
            it->second.postTransferWithRemoteIndex(localRegion, remoteIndex, remoteOffset, size, validSlices);

            // A completion will be posted to the completion queue, after which the counter will be decremented again
            pending++;
        }
        else
        {
            throw Exception::notFound("Target with id {} not found", targetId);
        }
    }

    // makeProgress
    bool RDMInitiator::makeProgress()
    {
        consolidateState();
        pollCQ();
        return hasPendingWork();
    }

    // makeProgressBlocking
    bool RDMInitiator::makeProgressBlocking(std::chrono::steady_clock::duration timeout)
    {
        auto now = std::chrono::steady_clock::now();
        consolidateState();
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

    RDMInitiator::RDMInitiator(std::shared_ptr<Endpoint> ep)
        : _endpoint(std::move(ep))
        , _localRegions(_endpoint->domain()->localRegions())
    {}

    bool RDMInitiator::hasPendingWork() const noexcept
    {
        return pending > 0;
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

        if (auto completion = _endpoint->completionQueue()->readBlocking(timeout); completion)
        {
            consume(*completion);
        }
    }

    void RDMInitiator::pollCQ()
    {
        if (auto completion = _endpoint->completionQueue()->read(); completion)
        {
            consume(*completion);
        }
    }

    void RDMInitiator::activateIdleEndpoints()
    {
        for (auto& [_, target] : _targets)
        {
            target.activate();
        }
    }

    void RDMInitiator::evictDeadEndpoints()
    {
        std::erase_if(_targets, [](auto const& item) { return item.second.canEvict(); });
    }

    void RDMInitiator::consolidateState()
    {
        activateIdleEndpoints();
        evictDeadEndpoints();
    }

    void RDMInitiator::consume(Completion completion)
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

    void RDMInitiator::handleCompletionData(Completion::Data)
    {
        if (pending == 0)
        {
            MXL_WARN("Received a completion but no transfer was pending");
            return;
        }

        --pending;
    }

    void RDMInitiator::handleCompletionError(Completion::Error err)
    {
        MXL_ERROR("TODO: handle completion error: {}", err.toString());
    }

} // namespace mxl::lib::fabrics::ofi
