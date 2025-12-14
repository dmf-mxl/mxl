// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "FabricInstance.hpp"
#include <cstdlib> // IWYU pragma: keep // for ::setenv
#include <mxl-internal/Logging.hpp>
#include "mxl/fabrics.h"
#include "Exception.hpp"
#include "FILogging.hpp"
#include "Initiator.hpp"
#include "Target.hpp"

namespace mxl::lib::fabrics::ofi
{
    FabricsInstance::FabricsInstance(mxl::lib::Instance* instance)
        : _mxlInstance(instance)
    {
        // Disable memory registration cache: Since we only perform memory registration during initialization rather than runtime, the cache provides
        // no benefit for our use case.
        ::setenv("FI_MR_CACHE_MONITOR", "disabled", 1);

        fiInitLogging();
    }

    mxlFabricsInstance FabricsInstance::toAPI() noexcept
    {
        return reinterpret_cast<mxlFabricsInstance>(this);
    }

    FabricsInstance* FabricsInstance::fromAPI(mxlFabricsInstance instance) noexcept
    {
        return reinterpret_cast<FabricsInstance*>(instance);
    }

    TargetWrapper* FabricsInstance::createTarget()
    {
        return &_targets.emplace_back();
    }

    InitiatorWrapper* FabricsInstance::createInitiator()
    {
        return &_initiators.emplace_back();
    }

    void FabricsInstance::destroyTarget(TargetWrapper* wrapper)
    {
        if (!_targets.remove_if([&](TargetWrapper const& lhs) { return &lhs == wrapper; }))
        {
            throw Exception::make(MXL_ERR_INVALID_ARG, "Target to remove is not known to instance");
        }
    }

    void FabricsInstance::destroyInitiator(InitiatorWrapper* initiator)
    {
        if (!_initiators.remove_if([&](InitiatorWrapper const& lhs) { return &lhs == initiator; }))
        {
            throw Exception::make(MXL_ERR_INVALID_ARG, "Initiator to remove is not known to instance");
        }
    }

}
