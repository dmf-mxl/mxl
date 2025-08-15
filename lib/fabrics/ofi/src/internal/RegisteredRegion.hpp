#pragma once

#include <vector>
#include <bits/types/struct_iovec.h>
#include "LocalRegion.hpp"
#include "MemoryRegion.hpp"
#include "Region.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    class RegisteredRegion
    {
    public:
        explicit RegisteredRegion(std::shared_ptr<MemoryRegion> memoryRegion, Region const& reg)
            : mr(std::move(memoryRegion))
            , region(reg)
        {}

        [[nodiscard]]
        RemoteRegion toRemote() const noexcept;

        [[nodiscard]]
        LocalRegion toLocal() const noexcept;

    private:
        std::shared_ptr<MemoryRegion> mr;
        Region region;
    };

    class RegisteredRegionGroup
    {
    public:
        explicit RegisteredRegionGroup(std::vector<RegisteredRegion> inner)
            : _inner(std::move(inner))
        {}

        [[nodiscard]]
        RemoteRegionGroup toRemote() const noexcept;

        [[nodiscard]]
        LocalRegionGroup toLocal() const noexcept;

    private:
        std::vector<RegisteredRegion> _inner;
    };

    std::vector<RemoteRegionGroup> toRemote(std::vector<RegisteredRegionGroup> const& groups) noexcept;
    std::vector<LocalRegionGroup> toLocal(std::vector<RegisteredRegionGroup> const& groups) noexcept;
}
