// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
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
#include "LocalRegion.hpp"
#include "RCTarget.hpp"
#include "RDMTarget.hpp"

namespace mxl::lib::fabrics::ofi
{
    LocalRegion Target::ImmediateDataLocation::toLocalRegion() noexcept
    {
        auto addr = &data;

        return LocalRegion{
            .addr = reinterpret_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(addr)),
            .len = sizeof(uint64_t),
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

    Target::ReadResult TargetWrapper::read()
    {
        if (!_inner)
        {
            Exception::invalidState("Target is not set up");
        }

        return _inner->read();
    }

    Target::ReadResult TargetWrapper::readBlocking(std::chrono::steady_clock::duration timeout)
    {
        if (!_inner)
        {
            Exception::invalidState("Target is not set up");
        }

        return _inner->readBlocking(timeout);
    }

    std::unique_ptr<TargetInfo> TargetWrapper::setup(mxlTargetConfig const& config)
    {
        if (_inner)
        {
            _inner.release();
        }

        switch (config.provider)
        {
            case MXL_SHARING_PROVIDER_AUTO:
            case MXL_SHARING_PROVIDER_TCP:
            case MXL_SHARING_PROVIDER_VERBS: {
                auto [target, info] = RCTarget::setup(config);
                _inner = std::move(target);
                return std::move(info);
            }

            case MXL_SHARING_PROVIDER_SHM:
            case MXL_SHARING_PROVIDER_EFA: {
                auto [target, info] = RDMTarget::setup(config);
                _inner = std::move(target);
                return std::move(info);
            }
        }

        throw Exception::invalidArgument("Invalid provider value");
    }

}
