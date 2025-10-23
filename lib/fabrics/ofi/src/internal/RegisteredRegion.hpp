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

    std::vector<RemoteRegion> toRemote(std::vector<RegisteredRegion> const& regions, bool useVirtualAddress) noexcept;
    std::vector<LocalRegion> toLocal(std::vector<RegisteredRegion> const& regions) noexcept;
}
