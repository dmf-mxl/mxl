// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RDMTarget.hpp"
#include <cstdint>
#include <memory>
#include <rdma/fabric.h>
#include "internal/Logging.hpp"
#include "AddressVector.hpp"
#include "Exception.hpp"
#include "FIInfo.hpp"
#include "Format.hpp" // IWYU pragma: keep; Includes template specializations of fmt::formatter for our types
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    std::pair<std::unique_ptr<RDMTarget>, std::unique_ptr<TargetInfo>> RDMTarget::setup(mxlTargetConfig const& config)
    {
        MXL_INFO("setting up target [endpoint = {}:{}, provider = {}]", config.endpointAddress.node, config.endpointAddress.service, config.provider);

        auto provider = providerFromAPI(config.provider);
        if (!provider)
        {
            throw Exception::invalidArgument("Invalid provider specified");
        }

        uint64_t caps = FI_RMA | FI_REMOTE_WRITE;
        caps |= config.deviceSupport ? FI_HMEM : 0;

        auto fabricInfoList = FIInfoList::get(config.endpointAddress.node, config.endpointAddress.service, provider.value(), caps, FI_EP_RDM);
        if (fabricInfoList.begin() == fabricInfoList.end())
        {
            throw Exception::make(MXL_ERR_NO_FABRIC, "No suitable fabric available");
        }

        auto info = *fabricInfoList.begin();
        MXL_DEBUG("{}", fi_tostr(info.raw(), FI_TYPE_INFO));

        auto fabric = Fabric::open(info);
        auto domain = Domain::open(fabric);
        if (config.regions != nullptr)
        {
            domain->registerRegionGroups(*RegionGroups::fromAPI(config.regions), FI_REMOTE_WRITE);
        }

        auto endpoint = Endpoint::create(domain);

        auto cq = CompletionQueue::open(domain, CompletionQueueAttr::defaults());
        endpoint.bind(cq, FI_RECV | FI_TRANSMIT);

        // Connectionless endpoints must be bound to an address vector. Even if it is not using the address vector.
        auto av = AddressVector::open(domain);
        endpoint.bind(av);

        // Connectionless endpoints must be explictely enabled when ready to used.
        endpoint.enable();

        std::unique_ptr<ImmediateDataLocation> dataRegion;
        if (endpoint.domain()->usingRecvBufForCqData())
        {
            // Create a local memory region. The grain indices will be written here when a transfer arrives.
            dataRegion = std::make_unique<ImmediateDataLocation>();

            // Post a receive for the first incoming grain. Pass a region to receive the grain index.
            endpoint.recv(dataRegion->toLocalRegion());
        }

        auto localAddress = endpoint.localAddress();

        struct MakeUniqueEnabler : RDMTarget
        {
            MakeUniqueEnabler(Endpoint endpoint, std::unique_ptr<ImmediateDataLocation> immData)
                : RDMTarget(std::move(endpoint), std::move(immData))
            {}
        };

        return {std::make_unique<MakeUniqueEnabler>(std::move(endpoint), std::move(dataRegion)),
            std::make_unique<TargetInfo>(std::move(localAddress), domain->RemoteRegionGroups())};
    }

    RDMTarget::RDMTarget(Endpoint endpoint, std::unique_ptr<ImmediateDataLocation> immData)
        : _endpoint(std::move(endpoint))
        , _immData(std::move(immData))
    {}

    Target::ReadResult RDMTarget::read()
    {
        return makeProgress<QueueReadMode::NonBlocking>({});
    }

    Target::ReadResult RDMTarget::readBlocking(std::chrono::steady_clock::duration timeout)
    {
        return makeProgress<QueueReadMode::Blocking>(timeout);
    }

    template<QueueReadMode queueReadMode>
    Target::ReadResult RDMTarget::makeProgress(std::chrono::steady_clock::duration timeout)
    {
        Target::ReadResult result;

        auto completion = readCompletionQueue<queueReadMode>(*_endpoint.completionQueue(), timeout);
        if (completion)
        {
            if (auto dataEntry = completion.value().tryData(); dataEntry)
            {
                // The written grain index is sent as immediate data, and was returned
                // from the completion queue.
                result.grainAvailable = dataEntry->data();

                // Need to post receive buffers for immediate data
                if (_endpoint.domain()->usingRecvBufForCqData())
                {
                    // Post another receive for the next incoming grain. When another transfer arrives,
                    // the immmediate data (in our case the grain index), will be returned in the registered region.
                    _endpoint.recv(_immData->toLocalRegion());
                }
            }
        }
        return result;
    }
}
