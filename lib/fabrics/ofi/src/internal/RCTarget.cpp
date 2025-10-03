// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RCTarget.hpp"
#include <cstdint>
#include <rdma/fabric.h>
#include "internal/Logging.hpp"
#include "mxl/mxl.h"
#include "Exception.hpp"
#include "Format.hpp" // IWYU pragma: keep; Includes template specializations of fmt::formatter for our types
#include "Region.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{

    std::pair<std::unique_ptr<RCTarget>, std::unique_ptr<TargetInfo>> RCTarget::setup(mxlTargetConfig const& config)
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
        auto fabricInfoList = FIInfoList::get(config.endpointAddress.node, config.endpointAddress.service, provider.value(), caps, FI_EP_MSG);

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
        if (config.regions != nullptr)
        {
            domain->registerRegionGroups(*RegionGroups::fromAPI(config.regions), FI_REMOTE_WRITE);
        }

        // Create a passive endpoint. A passive endpoint can be viewed like a bound TCP socket listening for
        // incoming connections
        auto pep = PassiveEndpoint::create(fabric);

        // Create an event queue for the passive endpoint. Incoming connections generate an entry in the event queue
        // and be picked up when the Target tries to make progress.
        pep.bind(EventQueue::open(fabric, EventQueueAttr::defaults()));

        // Transition the PassiveEndpoint into a listening state. Connections will be accepted from now on.
        pep.listen();

        // Helper struct to enable the std::make_unique function to access the private constructor of this class
        struct MakeUniqueEnabler : RCTarget
        {
            MakeUniqueEnabler(std::shared_ptr<Domain> domain, PassiveEndpoint pep)
                : RCTarget(std::move(domain), std::move(pep))
            {}
        };

        auto localAddress = pep.localAddress();
        auto remoteRegionGroups = domain->RemoteRegionGroups();

        // Return the constructed RCTarget and associated TargetInfo for remote peers to connect.
        return {std::make_unique<MakeUniqueEnabler>(std::move(domain), std::move(pep)),
            std::make_unique<TargetInfo>(std::move(localAddress), std::move(remoteRegionGroups))};
    }

    RCTarget::RCTarget(std::shared_ptr<Domain> domain, PassiveEndpoint ep)
        : _domain(std::move(domain))
        , _state(WaitForConnectionRequest{std::move(ep)})
    {}

    Target::ReadResult RCTarget::read()
    {
        return makeProgress<QueueReadMode::NonBlocking>({});
    }

    Target::ReadResult RCTarget::readBlocking(std::chrono::steady_clock::duration timeout)
    {
        return makeProgress<QueueReadMode::Blocking>(timeout);
    }

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
                        MXL_DEBUG("Connection request received, creating endpoint for remote address: {}", event->info().raw()->dest_addr);
                        auto endpoint = Endpoint::create(_domain, event->info());

                        auto cq = CompletionQueue::open(_domain, CompletionQueueAttr::defaults());
                        endpoint.bind(cq, FI_RECV);

                        auto eq = EventQueue::open(_domain->fabric(), EventQueueAttr::defaults());
                        endpoint.bind(eq);

                        // we are now ready to accept the connection
                        endpoint.accept();
                        MXL_DEBUG("Accepted the connection waiting for connected event notification.");

                        // Return the new state as the variant type
                        return RCTarget::WaitForConnection{std::move(endpoint)};
                    }

                    return state;
                },
                [&](WaitForConnection state) -> State
                {
                    auto event = readEventQueue<queueReadMode>(*state.ep.eventQueue(), timeout);

                    if (event && event->isConnected())
                    {
                        std::unique_ptr<RCTarget::ImmediateDataLocation> dataRegion;

                        // Need to post a receive buffer to get immediate data.
                        if (_domain->usingRecvBufForCqData())
                        {
                            // Create a local memory region. The grain indices will be written here when a transfer arrives.
                            dataRegion = std::make_unique<RCTarget::ImmediateDataLocation>();

                            // Post a receive for the first incoming grain. Pass a region to receive the grain index.
                            state.ep.recv(dataRegion->toLocalRegion());
                        }

                        MXL_INFO("Received connected event notification, now connected.");

                        // We have a connected event, so we can transition to the connected state
                        return Connected{.ep = std::move(state.ep), .immData = std::move(dataRegion)};
                    }

                    return state;
                },
                [&](RCTarget::Connected state) -> State
                {
                    auto [completion, event] = readEndpointQueues<queueReadMode>(state.ep, timeout);

                    if (event && event.value().isShutdown())
                    {
                        throw Exception::interrupted("Target received a shutdown event.");
                    }

                    if (completion)
                    {
                        if (auto dataEntry = completion.value().tryData(); dataEntry)
                        {
                            // The written grain index is sent as immediate data, and was returned
                            // from the completion queue.
                            result.grainAvailable = dataEntry->data();

                            // Need to post receive buffers for immediate data
                            if (_domain->usingRecvBufForCqData())
                            {
                                // Post another receive for the next incoming grain. When another transfer arrives,
                                // the immmediate data (in our case the grain index), will be returned in the registered region.
                                state.ep.recv(state.immData->toLocalRegion());
                            }
                        }
                        else
                        {
                            MXL_ERROR("CQ Error={}", completion->err().toString());
                        }
                    }

                    return state;
                }},
            std::move(_state));

        return result;
    }

}
