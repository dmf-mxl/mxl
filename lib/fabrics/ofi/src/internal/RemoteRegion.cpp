// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    ::fi_rma_iov RemoteRegion::toRmaIov() const noexcept
    {
        return ::fi_rma_iov{.addr = addr, .len = len, .key = rkey};
    }

    bool RemoteRegion::operator==(RemoteRegion const& other) const noexcept
    {
        return addr == other.addr && len == other.len && rkey == other.rkey;
    }

    ::fi_rma_iov const* RemoteRegionGroup::asRmaIovs() const noexcept
    {
        return _rmaIovs.data();
    }

    bool RemoteRegionGroup::operator==(RemoteRegionGroup const& other) const noexcept
    {
        return _inner == other._inner;
    }

    std::vector<::fi_rma_iov> RemoteRegionGroup::rmaIovsFromGroup(std::vector<RemoteRegion> group) noexcept
    {
        std::vector<::fi_rma_iov> rmaIovs;
        std::ranges::transform(group, std::back_inserter(rmaIovs), [](RemoteRegion const& reg) { return reg.toRmaIov(); });
        return rmaIovs;
    }

}
