// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "LocalRegion.hpp"
#include <algorithm>

namespace mxl::lib::fabrics::ofi
{

    ::iovec LocalRegion::toIov() const noexcept
    {
        return ::iovec{.iov_base = reinterpret_cast<void*>(addr), .iov_len = len};
    }

    std::vector<LocalRegion> const& LocalRegionGroup::view() const noexcept
    {
        return _inner;
    }

    ::iovec const* LocalRegionGroup::iovec() const noexcept
    {
        return _iovs.data();
    }

    size_t LocalRegionGroup::count() const noexcept
    {
        return _inner.size();
    }

    void* const* LocalRegionGroup::desc() const noexcept
    {
        return _descs.data();
    }

    std::vector<::iovec> LocalRegionGroup::iovFromGroup(std::vector<LocalRegion> group) noexcept
    {
        std::vector<::iovec> iovs;
        std::ranges::transform(group, std::back_inserter(iovs), [](LocalRegion const& reg) { return reg.toIov(); });
        return iovs;
    }

    std::vector<void*> LocalRegionGroup::descFromGroup(std::vector<LocalRegion> group) noexcept
    {
        std::vector<void*> descs;
        std::ranges::transform(group, std::back_inserter(descs), [](LocalRegion& reg) { return reg.desc; });
        return descs;
    }

} // namespace mxl::lib::fabrics::ofi
