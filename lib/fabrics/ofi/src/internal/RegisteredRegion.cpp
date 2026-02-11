// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/** \file RegisteredRegion.cpp
 * \brief Implementation of RegisteredRegion conversions to LocalRegion and RemoteRegion.
 */

#include "RegisteredRegion.hpp"
#include "LocalRegion.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    RemoteRegion RegisteredRegion::toRemote(bool useVirtualAddress) const noexcept
    {
        // Virtual addressing: remote uses actual pointer value (region.base)
        // Offset addressing: remote uses 0-based offset (0)
        auto addr = useVirtualAddress ? _region.base : 0;

        return RemoteRegion{.addr = addr, .len = _region.size, .rkey = _mr.rkey()};
    }

    LocalRegion RegisteredRegion::toLocal() const noexcept
    {
        // Local side always uses virtual address and memory descriptor (desc) for DMA
        return LocalRegion{.addr = _region.base, .len = _region.size, .desc = _mr.desc()};
    }

    std::vector<RemoteRegion> toRemote(std::vector<RegisteredRegion> const& groups, bool useVirtualAddress) noexcept
    {
        std::vector<RemoteRegion> remoteGroups;
        std::ranges::transform(
            groups, std::back_inserter(remoteGroups), [&](RegisteredRegion const& reg) { return reg.toRemote(useVirtualAddress); });
        return remoteGroups;
    }

    std::vector<LocalRegion> toLocal(std::vector<RegisteredRegion> const& groups) noexcept
    {
        std::vector<LocalRegion> localGroups;
        std::ranges::transform(groups, std::back_inserter(localGroups), [](RegisteredRegion const& reg) { return reg.toLocal(); });
        return localGroups;
    }

} // namespace mxl::lib::fabrics::ofi
