// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>
#include <bits/types/struct_iovec.h>
#include "Domain.hpp"
#include "LocalRegion.hpp"
#include "MemoryRegion.hpp"
#include "Region.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    class RegisteredRegion
    {
    public:
        explicit RegisteredRegion(MemoryRegion memoryRegion, Region reg)
            : _mr(std::move(memoryRegion))
            , _region(std::move(reg))
        {}

        [[nodiscard]]
        RemoteRegion toRemote(bool useVirtualAddress) const noexcept;

        [[nodiscard]]
        LocalRegion toLocal() const noexcept;

    private:
        MemoryRegion _mr;
        Region _region;
    };

    class RegisteredRegionGroup
    {
    public:
        explicit RegisteredRegionGroup(std::vector<RegisteredRegion> inner)
            : _inner(std::move(inner))
        {}

        [[nodiscard]]
        RemoteRegionGroup toRemote(bool useVirtualAddress) const noexcept;

        [[nodiscard]]
        LocalRegionGroup toLocal() const noexcept;

    private:
        std::vector<RegisteredRegion> _inner;
    };

    std::vector<RemoteRegionGroup> toRemote(std::vector<RegisteredRegionGroup> const& groups, bool useVirtualAddress) noexcept;
    std::vector<LocalRegionGroup> toLocal(std::vector<RegisteredRegionGroup> const& groups) noexcept;
}
