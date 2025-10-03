// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RegisteredRegion.hpp"
#include "LocalRegion.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    RemoteRegion RegisteredRegion::toRemote(bool useVirtualAddress) const noexcept
    {
        auto addr = useVirtualAddress ? _region.base : 0;

        return RemoteRegion{.addr = addr, .len = _region.size, .rkey = _mr.rkey()};
    }

    LocalRegion RegisteredRegion::toLocal() const noexcept
    {
        return LocalRegion{.addr = _region.base, .len = _region.size, .desc = _mr.desc()};
    }

    RemoteRegionGroup RegisteredRegionGroup::toRemote(bool useVirtualAddress) const noexcept
    {
        std::vector<RemoteRegion> group;

        std::ranges::transform(_inner, std::back_inserter(group), [&](RegisteredRegion const& reg) { return reg.toRemote(useVirtualAddress); });

        return RemoteRegionGroup{group};
    }

    LocalRegionGroup RegisteredRegionGroup::toLocal() const noexcept
    {
        std::vector<LocalRegion> group;

        std::ranges::transform(_inner, std::back_inserter(group), [](RegisteredRegion const& reg) { return reg.toLocal(); });

        return LocalRegionGroup{group};
    }

    std::vector<RemoteRegionGroup> toRemote(std::vector<RegisteredRegionGroup> const& groups, bool useVirtualAddress) noexcept
    {
        std::vector<RemoteRegionGroup> remoteGroups;
        std::ranges::transform(
            groups, std::back_inserter(remoteGroups), [&](RegisteredRegionGroup const& reg) { return reg.toRemote(useVirtualAddress); });
        return remoteGroups;
    }

    std::vector<LocalRegionGroup> toLocal(std::vector<RegisteredRegionGroup> const& groups) noexcept
    {
        std::vector<LocalRegionGroup> localGroups;
        std::ranges::transform(groups, std::back_inserter(localGroups), [](RegisteredRegionGroup const& reg) { return reg.toLocal(); });
        return localGroups;
    }

} // namespace mxl::lib::fabrics::ofi
