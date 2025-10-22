// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <uuid.h>
#include <sys/types.h>
#include <fmt/format.h>
#include "mxl/fabrics.h"
#include "Address.hpp"
#include "Endpoint.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    // TargetInfo contains all the information required by an initiator to operate transfers to the given target.
    // In the context of libfabric this means, the fi_addr, all the buffer addresses and sizes, and the remote protection key.
    struct TargetInfo
    {
    public:
        static TargetInfo* fromAPI(mxlTargetInfo api) noexcept;

        [[nodiscard]]
        ::mxlTargetInfo toAPI() noexcept;

        [[nodiscard]]
        std::string toJSON() const;

        static TargetInfo fromJSON(std::string const& s);

        bool operator==(TargetInfo const& other) const noexcept;

    public:
        FabricAddress fabricAddress;
        std::vector<RemoteRegion> remoteRegions;
        Endpoint::Id id;
    };
}
