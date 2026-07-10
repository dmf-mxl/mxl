// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
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
#include "FabricAddress.hpp"
#include "FabricInfo.hpp"
#include "Format.hpp" // IWYU pragma: keep; Includes template specializations of fmt::formatter for our types
#include "Protocol.hpp"
#include "Provider.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    std::pair<std::unique_ptr<RDMTarget>, std::unique_ptr<TargetInfo>> RDMTarget::setup(mxlFabricsTargetConfig const& config, FabricInfoView info,
        TargetSetupOptions const& options)
    {
        auto const provider = providerFromString(info->fabric_attr->prov_name);
        if (!provider)
        {
            throw Exception::invalidArgument("invalid provider: {}", info->fabric_attr->prov_name);
        }

        MXL_INFO("Setting up RDM target with source address: {}", FabricAddress::fromSource(info).toString());

        auto fabric = Fabric::open(info);
        auto domain = Domain::open(fabric);

        auto endpoint = Endpoint::create(domain);

        auto cqAttr = CompletionQueue::Attributes::defaults();
        if (options.cqDepth)
        {
            cqAttr.size = *options.cqDepth;
        }
        if (provider == Provider::EFA)
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

        auto cq = CompletionQueue::open(domain, cqAttr);
        endpoint.bind(cq, FI_RECV | FI_TRANSMIT);

        // Connectionless endpoints must be bound to an address vector. Even if it is not using the address vector.
        auto av = AddressVector::open(domain);
        endpoint.bind(av);

        // Connectionless endpoints must be explictely enabled when they are ready to be used.
        endpoint.enable();

        auto mxlRegions = MxlRegions::forWriter(config.writer);
        auto protocol = selectIngressProtocol(mxlRegions.dataLayout(), mxlRegions.regions(), mxlRegions.maxSyncBatchSize());
        auto targetInfo = std::make_unique<TargetInfo>(
            endpoint.id(), endpoint.localAddress(), *provider, protocol->registerMemory(domain), protocol->bounceBufferInfo());

        struct MakeUniqueEnabler : RDMTarget
        {
            MakeUniqueEnabler(Endpoint ep, std::unique_ptr<IngressProtocol> proto)
                : RDMTarget(std::move(ep), std::move(proto))
            {}
        };

        return {std::make_unique<MakeUniqueEnabler>(std::move(endpoint), std::move(protocol)), std::move(targetInfo)};
    }

    RDMTarget::RDMTarget(Endpoint ep, std::unique_ptr<IngressProtocol> proto)
        : _ep(std::move(ep))
        , _protocol(std::move(proto))
    {}

    std::optional<Target::GrainReadResult> RDMTarget::readGrain()
    {
        if (!_protocol->canReadGrains())
        {
            throw Exception::unsupportedOperation("The current protocol does not support reading grains.");
        }

        if (auto res = readNext<QueueReadMode::NonBlocking>({}); res)
        {
            return std::get<Target::GrainReadResult>(*res);
        }
        return std::nullopt;
    }

    std::optional<Target::GrainReadResult> RDMTarget::readGrainBlocking(std::chrono::steady_clock::duration timeout)
    {
        if (!_protocol->canReadGrains())
        {
            throw Exception::unsupportedOperation("The current protocol does not support reading grains.");
        }

        if (auto res = readNext<QueueReadMode::Blocking>(timeout); res)
        {
            return std::get<Target::GrainReadResult>(*res);
        }
        return std::nullopt;
    }

    std::optional<Target::SampleReadResult> RDMTarget::readSamples()
    {
        if (!_protocol->canReadSamples())
        {
            throw Exception::unsupportedOperation("The current protocol does not support reading samples.");
        }

        if (auto res = readNext<QueueReadMode::NonBlocking>({}); res)
        {
            return std::get<Target::SampleReadResult>(*res);
        }
        return std::nullopt;
    }

    std::optional<Target::SampleReadResult> RDMTarget::readSamplesBlocking(std::chrono::steady_clock::duration timeout)
    {
        if (!_protocol->canReadSamples())
        {
            throw Exception::unsupportedOperation("The current protocol does not support reading samples.");
        }

        if (auto res = readNext<QueueReadMode::Blocking>(timeout); res)
        {
            return std::get<Target::SampleReadResult>(*res);
        }
        return std::nullopt;
    }

    void RDMTarget::shutdown()
    {}

    template<QueueReadMode queueReadMode>
    std::optional<Target::ReadResult> RDMTarget::readNext(std::chrono::steady_clock::duration timeout)
    {
        auto completion = readCompletionQueue<queueReadMode>(*_ep.completionQueue(), timeout);
        if (completion)
        {
            return _protocol->read(_ep, *completion);
        }

        return {};
    }

}
