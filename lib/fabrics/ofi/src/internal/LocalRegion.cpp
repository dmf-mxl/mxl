// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "LocalRegion.hpp"
#include <algorithm>

namespace mxl::lib::fabrics::ofi
{

    LocalRegion LocalRegion::sub(std::uint64_t offset, std::size_t length) const noexcept
    {
        assert(offset + length <= len);

        return LocalRegion{
            .addr = addr + offset,
            .len = length,
            .desc = desc,
        };
    }

    ::iovec LocalRegion::toIovec() const noexcept
    {
        return ::iovec{.iov_base = reinterpret_cast<void*>(addr), .iov_len = len};
    }

    ::iovec const* LocalRegionGroup::asIovec() const noexcept
    {
        return _iovs.data();
    }

    void* const* LocalRegionGroup::desc() const noexcept
    {
        return _descs.data();
    }

    std::vector<::iovec> LocalRegionGroup::iovFromGroup(std::vector<LocalRegion> group) noexcept
    {
        std::vector<::iovec> iovs;
        std::ranges::transform(group, std::back_inserter(iovs), [](LocalRegion const& reg) { return reg.toIovec(); });
        return iovs;
    }

    std::vector<void*> LocalRegionGroup::descFromGroup(std::vector<LocalRegion> group) noexcept
    {
        std::vector<void*> descs;
        std::ranges::transform(group, std::back_inserter(descs), [](LocalRegion& reg) { return reg.desc; });
        return descs;
    }
} // namespace mxl::lib::fabrics::ofi
