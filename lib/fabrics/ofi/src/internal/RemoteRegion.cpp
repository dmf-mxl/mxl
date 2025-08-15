#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    ::fi_rma_iov RemoteRegion::toRmaIov() const noexcept
    {
        return ::fi_rma_iov{.addr = addr, .len = len, .key = rkey};
    }

    std::vector<RemoteRegion> const& RemoteRegionGroup::view() const noexcept
    {
        return _inner;
    }

    ::fi_rma_iov const* RemoteRegionGroup::rmaIovs() const noexcept
    {
        return _rmaIovs.data();
    }

    size_t RemoteRegionGroup::count() const noexcept
    {
        return _inner.size();
    }

    std::vector<::fi_rma_iov> RemoteRegionGroup::rmaIovsFromGroup(std::vector<RemoteRegion> group) noexcept
    {
        std::vector<::fi_rma_iov> rmaIovs;
        std::ranges::transform(group, std::back_inserter(rmaIovs), [](RemoteRegion const& reg) { return reg.toRmaIov(); });
        return rmaIovs;
    }

}
