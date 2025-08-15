#include "RegisteredRegion.hpp"
#include "LocalRegion.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    RemoteRegion RegisteredRegion::toRemote() const noexcept
    {
        auto addr = mr->_domain->usingVirtualAddresses() ? region.base : 0;

        return RemoteRegion{.addr = addr, .len = region.size, .rkey = mr->rkey()};
    }

    LocalRegion RegisteredRegion::toLocal() const noexcept
    {
        return LocalRegion{.addr = region.base, .len = region.size, .desc = mr->desc()};
    }

    RemoteRegionGroup RegisteredRegionGroup::toRemote() const noexcept
    {
        std::vector<RemoteRegion> group;

        std::ranges::transform(_inner, std::back_inserter(group), [](RegisteredRegion const& reg) { return reg.toRemote(); });

        return RemoteRegionGroup{group};
    }

    LocalRegionGroup RegisteredRegionGroup::toLocal() const noexcept
    {
        std::vector<LocalRegion> group;

        std::ranges::transform(_inner, std::back_inserter(group), [](RegisteredRegion const& reg) { return reg.toLocal(); });

        return LocalRegionGroup{group};
    }

    std::vector<RemoteRegionGroup> toRemote(std::vector<RegisteredRegionGroup> const& groups) noexcept
    {
        std::vector<RemoteRegionGroup> remoteGroups;
        std::ranges::transform(groups, std::back_inserter(remoteGroups), [](RegisteredRegionGroup const& reg) { return reg.toRemote(); });
        return remoteGroups;
    }

    std::vector<LocalRegionGroup> toLocal(std::vector<RegisteredRegionGroup> const& groups) noexcept
    {
        std::vector<LocalRegionGroup> localGroups;
        std::ranges::transform(groups, std::back_inserter(localGroups), [](RegisteredRegionGroup const& reg) { return reg.toLocal(); });
        return localGroups;
    }

} // namespace mxl::lib::fabrics::ofi
