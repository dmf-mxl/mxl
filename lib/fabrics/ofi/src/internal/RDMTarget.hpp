// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include "mxl/fabrics.h"
#include "Endpoint.hpp"
#include "QueueHelpers.hpp"
#include "RegisteredRegion.hpp"
#include "Target.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    class RDMTarget : public Target
    {
    public:
        static std::pair<std::unique_ptr<RDMTarget>, std::unique_ptr<TargetInfo>> setup(mxlTargetConfig const&);

        Target::ReadResult read() final;
        Target::ReadResult readBlocking(std::chrono::steady_clock::duration timeout) final;

    private:
        RDMTarget(Endpoint endpoint, std::unique_ptr<ImmediateDataLocation> immData);

        template<QueueReadMode>
        Target::ReadResult makeProgress(std::chrono::steady_clock::duration timeout);

    private:
        Endpoint _endpoint;
        std::vector<RegisteredRegionGroup> _regRegions;
        std::unique_ptr<ImmediateDataLocation> _immData;
    };
}
