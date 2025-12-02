// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RCTarget.hpp"
#include <cstdint>
#include <mxl-internal/Logging.hpp>
#include <rdma/fabric.h>
#include "mxl/mxl.h"
#include "Exception.hpp"
#include "FabricInfo.hpp"
#include "Format.hpp" // IWYU pragma: keep; Includes template specializations of fmt::formatter for our types
#include "PassiveEndpoint.hpp"
#include "Protocol.hpp"
#include "Region.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{

    std::pair<std::unique_ptr<RCTarget>, std::unique_ptr<TargetInfo>> RCTarget::setup(mxlFabricsTargetConfig const& config)
    {
        MXL_INFO("setting up target [endpoint = {}:{}, provider = {}]", config.endpointAddress.node, config.endpointAddress.service, config.provider);

        // Convert to our internal enum type
        auto provider = providerFromAPI(config.provider);
        if (!provider)
        {
            throw Exception::invalidArgument("Invalid provider passed");
        }

        uint64_t caps = FI_RMA | FI_REMOTE_WRITE;
        caps |= config.deviceSupport ? FI_HMEM : 0;

        // Get a list of available fabric configurations available on this machine.
        auto fabricInfoList = FabricInfoList::get(config.endpointAddress.node, config.endpointAddress.service, provider.value(), caps, FI_EP_MSG);

        if (fabricInfoList.begin() == fabricInfoList.end())
        {
            throw Exception::make(MXL_ERR_NO_FABRIC,
                "No fabric available for provider {} at {}:{}",
                config.provider,
                config.endpointAddress.node,
                config.endpointAddress.service);
        }

        // Open fabric and domain. These represent the context of the local network fabric adapter that will be used
        // to receive data.
        // See fi_domain(3) and fi_fabric(3) for more complete information about these concepts.
        auto fabric = Fabric::open(*fabricInfoList.begin());
        auto domain = Domain::open(fabric);

        auto pep = makeListener(fabric);

        auto const mxlRegions = MxlRegions::fromAPI(config.regions);
        auto proto = selectIngressProtocol(mxlRegions->dataLayout(), mxlRegions->regions());
        auto targetInfo = std::make_unique<TargetInfo>(pep.id(), pep.localAddress(), proto->registerMemory(domain));

        // Helper struct to enable the std::make_unique function to access the private constructor of this class
        struct MakeUniqueEnabler : RCTarget
        {
            MakeUniqueEnabler(PassiveEndpoint pep, std::unique_ptr<IngressProtocol> proto, std::shared_ptr<Domain> domain)
                : RCTarget(std::move(pep), std::move(proto), std::move(domain))
            {}
        };

        // Return the constructed RCTarget and associated TargetInfo for remote peers to connect.
        return {std::make_unique<MakeUniqueEnabler>(std::move(pep), std::move(proto), std::move(domain)), std::move(targetInfo)};
    }

    RCTarget::RCTarget(PassiveEndpoint pep, std::unique_ptr<IngressProtocol> proto, std::shared_ptr<Domain> domain)
        : _proto(std::move(proto))
        , _domain(std::move(domain))
        , _state(WaitForConnectionRequest{std::move(pep)})
    {}

    Target::ReadResult RCTarget::read()
    {
        return makeProgress<QueueReadMode::NonBlocking>({});
    }

    Target::ReadResult RCTarget::readBlocking(std::chrono::steady_clock::duration timeout)
    {
        return makeProgress<QueueReadMode::Blocking>(timeout);
    }

    void RCTarget::shutdown()
    {}

    template<QueueReadMode queueReadMode>
    Target::ReadResult RCTarget::makeProgress(std::chrono::steady_clock::duration timeout)
    {
        Target::ReadResult result;

        _state = std::visit(
            overloaded{[](std::monostate) -> State { throw Exception::invalidState("Target is in an invalid state an can no longer make progress"); },
                [&](WaitForConnectionRequest state) -> State
                {
                    auto event = readEventQueue<queueReadMode>(*state.pep.eventQueue(), timeout);

                    // Check if the entry is available and is a connection request
                    if (event && event->isConnReq())
                    {
                        MXL_DEBUG("Connection request received, creating endpoint for remote address: {}", event->connReq().info().raw()->dest_addr);
                        auto endpoint = Endpoint::create(_domain, state.pep.id(), event->connReq().info());

                        auto cq = CompletionQueue::open(_domain, CompletionQueue::Attributes::defaults());
                        endpoint.bind(cq, FI_RECV);

                        auto eq = EventQueue::open(_domain->fabric(), EventQueue::Attributes::defaults());
                        endpoint.bind(eq);

                        // we are now ready to accept the connection
                        endpoint.accept();
                        MXL_DEBUG("Accepted the connection waiting for connected event notification.");

                        // Return the new state as the variant type
                        return RCTarget::WaitForConnection{std::move(endpoint)};
                    }

                    return WaitForConnectionRequest{.pep = std::move(state.pep)};
                },
                [&](WaitForConnection state) -> State
                {
                    auto event = readEventQueue<queueReadMode>(*state.ep.eventQueue(), timeout);

                    if (event && event->isConnected())
                    {
                        MXL_INFO("Received connected event notification, now connected.");

                        // We have a connected event, so we can transition to the connected state
                        auto connected = Connected{.ep = std::move(state.ep)};

                        // The endpoint is now ready, initialize the protocol.
                        _proto->start(connected.ep);

                        return connected;
                    }

                    return WaitForConnection{std::move(state.ep)};
                },
                [&](RCTarget::Connected state) -> State
                {
                    auto [completion, event] = readEndpointQueues<queueReadMode>(state.ep, timeout);

                    if (event && event.value().isShutdown())
                    {
                        MXL_INFO("Remote endpoint has shutdown the connection. Transitioning to listening to new connection.");
                        return WaitForConnectionRequest{.pep = makeListener(state.ep.domain()->fabric())};
                    }

                    if (completion)
                    {
                        result = _proto->processCompletion(state.ep, *completion);
                    }

                    return Connected{.ep = std::move(state.ep)};
                }},
            std::move(_state));

        return result;
    }

    PassiveEndpoint RCTarget::makeListener(std::shared_ptr<Fabric> fabric)
    {
        // Create a passive endpoint. A passive endpoint can be viewed like a bound TCP socket listening for
        // incoming connections
        auto pep = PassiveEndpoint::create(fabric);

        // Create an event queue for the passive endpoint. Incoming connections generate an entry in the event queue
        // and be picked up when the Target tries to make progress.
        pep.bind(EventQueue::open(fabric, EventQueue::Attributes::defaults()));

        // Transition the PassiveEndpoint into a listening state. Connections will be accepted from now on.
        pep.listen();

        return pep;
    }
}
