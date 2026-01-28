// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "RemoteRegion.hpp"
#include <cassert>
#include <algorithm>
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{

    RemoteRegion RemoteRegion::sub(std::uint64_t offset, std::size_t length) const
    {
        if (offset + length > len)
        {
            throw Exception::invalidArgument("Invalid offset and length for remote region");
        }

        return RemoteRegion{
            .addr = addr + offset,
            .len = length,
            .rkey = rkey,
        };
    }

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
}
