// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RDMTarget.hpp"
#include <cstdint>
#include <memory>
#include <mxl-internal/Logging.hpp>
#include <rdma/fabric.h>
#include <rdma/fi_eq.h>
#include "AddressVector.hpp"
#include "Exception.hpp"
#include "FabricInfo.hpp"
#include "Format.hpp" // IWYU pragma: keep; Includes template specializations of fmt::formatter for our types
#include "Protocol.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    std::pair<std::unique_ptr<RDMTarget>, std::unique_ptr<TargetInfo>> RDMTarget::setup(mxlFabricsTargetConfig const& config)
    {
        MXL_INFO("setting up target [endpoint = {}:{}, provider = {}]", config.endpointAddress.node, config.endpointAddress.service, config.provider);

        auto provider = providerFromAPI(config.provider);
        if (!provider)
        {
            throw Exception::invalidArgument("Invalid provider specified");
        }

        uint64_t caps = FI_RMA | FI_REMOTE_WRITE;
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

        auto cqAttr = CompletionQueue::Attributes::defaults();
        if (provider == Provider::EFA)
        {
            cqAttr.waitObject = FI_WAIT_NONE;
        }
        auto cq = CompletionQueue::open(domain, cqAttr);
        endpoint.bind(cq, FI_RECV | FI_TRANSMIT);

        // Connectionless endpoints must be bound to an address vector. Even if it is not using the address vector.
        auto av = AddressVector::open(domain);
        endpoint.bind(av);

        // Connectionless endpoints must be explictely enabled when they are ready to be used.
        endpoint.enable();

        auto mxlRegions = MxlRegions::fromAPI(config.regions);
        auto protocol = selectIngressProtocol(mxlRegions->dataLayout(), mxlRegions->regions());
        auto targetInfo = std::make_unique<TargetInfo>(endpoint.id(), endpoint.localAddress(), protocol->registerMemory(domain));

        struct MakeUniqueEnabler : RDMTarget
        {
            MakeUniqueEnabler(Endpoint ep, std::unique_ptr<IngressProtocol> proto)
                : RDMTarget(std::move(ep), std::move(proto))
            {}
        };

        return {std::make_unique<MakeUniqueEnabler>(std::move(endpoint), std::move(protocol)), std::move(targetInfo)};
    }

    RDMTarget::RDMTarget(Endpoint ep, std::unique_ptr<IngressProtocol> ingress)
        : _ep(std::move(ep))
        , _protocol(std::move(ingress))
    {}

    Target::ReadResult RDMTarget::read()
    {
        return makeProgress<QueueReadMode::NonBlocking>({});
    }

    Target::ReadResult RDMTarget::readBlocking(std::chrono::steady_clock::duration timeout)
    {
        return makeProgress<QueueReadMode::Blocking>(timeout);
    }

    void RDMTarget::shutdown()
    {}

    template<QueueReadMode queueReadMode>
    Target::ReadResult RDMTarget::makeProgress(std::chrono::steady_clock::duration timeout)
    {
        auto completion = readCompletionQueue<queueReadMode>(*_ep.completionQueue(), timeout);
        if (completion)
        {
            return _protocol->processCompletion(_ep, *completion);
        }

        return ReadResult{std::nullopt};
    }

}
