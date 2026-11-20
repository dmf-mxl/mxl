// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Target.hpp"
#include <memory>
#include <utility>
#include <fmt/format.h>
#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include "mxl/fabrics.h"
#include "Exception.hpp"
#include "FabricInfoHelpers.hpp"
#include "FabricInterfaceProbe.hpp"
#include "LocalRegion.hpp"
#include "RCTarget.hpp"
#include "RDMTarget.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    LocalRegion Target::ImmediateDataLocation::toLocalRegion() const noexcept
    {
        return LocalRegion{
            .addr = std::bit_cast<std::uint64_t>(&data),
            .len = sizeof(std::uint64_t),
            .desc = nullptr,
        };
    }

    TargetWrapper* TargetWrapper::fromAPI(mxlFabricsTarget api) noexcept
    {
        return reinterpret_cast<TargetWrapper*>(api);
    }

    mxlFabricsTarget TargetWrapper::toAPI() noexcept
    {
        return reinterpret_cast<mxlFabricsTarget>(this);
    }

    std::optional<Target::ReadResult> TargetWrapper::read()
    {
        if (!_inner)
        {
            throw Exception::invalidState("Target is not set up.");
        }

        return _inner->read();
    }

    std::optional<Target::ReadResult> TargetWrapper::readBlocking(std::chrono::steady_clock::duration timeout)
    {
        if (!_inner)
        {
            throw Exception::invalidState("Target is not set up.");
        }

        return _inner->readBlocking(timeout);
    }

    template<typename TargetT>
    std::unique_ptr<TargetInfo> TargetWrapper::setup(mxlFabricsTargetConfig const& config, FabricInfoView info, TargetSetupOptions const& options)
    {
        auto [inner, targetInfo] = TargetT::setup(config, info, options);
        _inner = std::move(inner);
        return std::move(targetInfo);
    }

    std::unique_ptr<TargetInfo> TargetWrapper::setup(mxlFabricsTargetConfig const& config, TargetSetupOptions const& options)
    {
        if (_inner)
        {
            _inner.reset();
        }

        // Regions in device memory need an interface that can register them (FI_HMEM). The regions come from the flow the target writes.
        auto const needsHmem = regionsNeedHmem(MxlRegions::forWriter(config.writer).regions());
        auto const interfaceConfig = interfaceConfigWithHmemIfNeeded(config.interface, needsHmem);
        auto [info, providerConfig] = selectSourceInterface(interfaceConfig, /* target */ true);
        (void)providerConfig;

        if (needsHmem)
        {
            requireCapability(info.view(), FI_HMEM, "Selected fabric interface does not support FI_HMEM required by device grain payloads");
        }

        switch (info->ep_attr->type)
        {
            case FI_EP_MSG: return setup<RCTarget>(config, info.view(), options);
            case FI_EP_RDM: return setup<RDMTarget>(config, info.view(), options);
            default:        throw Exception::invalidState("unsupported endpoint type");
        }
    }
}
