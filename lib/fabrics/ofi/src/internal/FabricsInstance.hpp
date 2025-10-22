// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <list>
#include <mxl-internal/Instance.hpp>
#include <mxl/fabrics.h>
#include "Initiator.hpp"
#include "Target.hpp"

namespace mxl::lib::fabrics::ofi
{

    class FabricsInstance
    {
    public:
        FabricsInstance(mxl::lib::Instance* instance);
        ~FabricsInstance() = default;

        FabricsInstance(FabricsInstance&&) = delete;
        FabricsInstance(FabricsInstance const&) = delete;
        FabricsInstance& operator=(FabricsInstance&&) = delete;
        FabricsInstance& operator=(FabricsInstance const&) = delete;

        mxlFabricsInstance toAPI() noexcept;
        static FabricsInstance* fromAPI(mxlFabricsInstance) noexcept;

        TargetWrapper* createTarget();
        void destroyTarget(TargetWrapper*);
        InitiatorWrapper* createInitiator();
        void destroyInitiator(InitiatorWrapper*);

    private:
        mxl::lib::Instance* _mxlInstance;
        std::list<TargetWrapper> _targets;
        std::list<InitiatorWrapper> _initiators;
    };

}
