#include "Target.hpp"
#include <memory>
#include <fmt/format.h>
#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include "mxl/fabrics.h"
#include "Exception.hpp"
#include "RCTarget.hpp"

namespace mxl::lib::fabrics::ofi
{

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
            case MXL_SHARING_PROVIDER_AUTO: {
                auto [target, info] = RCTarget::setup(config);
                _inner = std::move(target);
                return std::move(info);
            }
            case MXL_SHARING_PROVIDER_TCP: {
                auto [target, info] = RCTarget::setup(config);
                _inner = std::move(target);
                return std::move(info);
            }
            case MXL_SHARING_PROVIDER_VERBS: {
                auto [target, info] = RCTarget::setup(config);
                _inner = std::move(target);
                return std::move(info);
            }
            case MXL_SHARING_PROVIDER_EFA: Exception::internal("The EFA provider is not yet implemented");
        }

        throw Exception::invalidArgument("Invalid provider value");
    }

}
