// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <variant>
#include "mxl/fabrics.h"
#include "Endpoint.hpp"
#include "PassiveEndpoint.hpp"
#include "QueueHelpers.hpp"
#include "Target.hpp"

namespace mxl::lib::fabrics::ofi
{
    // Realiable+Connected Target implementation
    class RCTarget : public Target
    {
    public:
        static std::pair<std::unique_ptr<RCTarget>, std::unique_ptr<TargetInfo>> setup(mxlTargetConfig const&);

        Target::ReadResult read() final;
        Target::ReadResult readBlocking(std::chrono::steady_clock::duration timeout) final;

    private:
        struct WaitForConnectionRequest
        {
            PassiveEndpoint pep;
        };

        struct WaitForConnection
        {
            Endpoint ep;
        };

        struct Connected
        {
            Endpoint ep;
            std::unique_ptr<ImmediateDataLocation> immData;
        };

        using State = std::variant<WaitForConnectionRequest, WaitForConnection, Connected>;

    private:
        RCTarget(std::shared_ptr<Domain> domain, PassiveEndpoint pep);

        template<QueueReadMode>
        Target::ReadResult makeProgress(std::chrono::steady_clock::duration timeout);

    private:
        std::shared_ptr<Domain> _domain;

        State _state;
    };
}
