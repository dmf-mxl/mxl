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
#include "FabricInfo.hpp"
#include "Format.hpp" // IWYU pragma: keep; Includes template specializations of fmt::formatter for our types
#include "Protocol.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    std::pair<std::unique_ptr<RDMTarget>, std::unique_ptr<TargetInfo>> RDMTarget::setup(mxlFabricsTargetConfig const& config,
        TargetSetupOptions const& options)
    {
        // Both fields may arrive as null at this call site — libfabric's FI_SOURCE path
        // accepts that as "bind to any local address". fmt's `{}` formatter for char const*
        // throws fmt::format_error("string pointer is null") on a nullptr, which MXL_INFO
        // silently swallows. Substitute a printable sentinel for the log only — the original
        // pointers still go to fi_getinfo so the wildcard semantics are preserved.
        char const* const nodeStr = config.interface.address.node ? config.interface.address.node : "<unspecified>";
        char const* const serviceStr = config.interface.address.service ? config.interface.address.service : "<unspecified>";

        MXL_INFO("setting up target [endpoint = {}:{}, provider = {}]", nodeStr, serviceStr, config.interface.provider);

        auto provider = providerFromAPI(config.interface.provider);
        if (!provider)
        {
            throw Exception::invalidArgument("Invalid provider specified");
        }

        std::uint64_t caps = FI_RMA | FI_REMOTE_WRITE;
        // To enable device memory support:
        // caps |=  FI_HMEM;
        auto fabricInfoList = FabricInfoList::get(config.interface.address.node, config.interface.address.service, provider.value(), caps, FI_EP_RDM);
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
        if (options.cqDepth)
        {
            cqAttr.size = *options.cqDepth;
        }
        if (provider == Provider::EFA)
        {
            if (!CompletionQueue::isWaitObjectSupportedForEFA())
            {
                MXL_WARN("Wait objects not supported in EFA provider for this libfabric version. Only non-blocking API available.");
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
            endpoint.id(), endpoint.localAddress(), protocol->registerMemory(domain), protocol->bounceBufferInfo());

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
