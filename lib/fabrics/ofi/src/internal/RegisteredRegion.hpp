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
    /** \brief Represent a registered memory region.
     */
    class RegisteredRegion
    {
    public:
        explicit RegisteredRegion(MemoryRegion memoryRegion, Region reg)
            : _mr(std::move(memoryRegion))
            , _region(std::move(reg))
        {}

        /** \brief Generate a RemoteRegion from this RegisteredRegion.
         *
         * \param useVirtualAddress If true, the RemoteRegion will use the virtual address.
         *                          If false, the RemoteRegion will use a zero-based address.
         *
         * \return The generated RemoteRegion.
         */
        [[nodiscard]]
        RemoteRegion toRemote(bool useVirtualAddress) const noexcept;

        /** \brief Generate a LocalRegion from this RegisteredRegion.
         */
        [[nodiscard]]
        LocalRegion toLocal() const noexcept;

    private:
        MemoryRegion _mr;
        Region _region;
    };

    /** Generate a list of RemoteRegions from a list of RegisteredRegions.
     *
     *  \param useVirtualAddress If true, the RemoteRegions will use the virtual addresses.
     *                           If false, the RemoteRegions will use zero-based addresses.
     */
    std::vector<RemoteRegion> toRemote(std::vector<RegisteredRegion> const& regions, bool useVirtualAddress) noexcept;

    /** Generate a list of LocalRegions from a list of RegisteredRegions.
     */
    std::vector<LocalRegion> toLocal(std::vector<RegisteredRegion> const& regions) noexcept;
}
