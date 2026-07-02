// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <sys/uio.h>
#include "Domain.hpp"
#include "LocalRegion.hpp"
#include "MemoryRegion.hpp"
#include "Region.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief Represent a registered memory region.
     *
     * Several registered regions may share a single underlying \c MemoryRegion
     * when their memory is contiguous (see Domain::registerRegions). In that
     * case \c mrOffset is the byte offset of this region from the start of the
     * shared registration, used to compute the remote address when the provider
     * does not use virtual addressing.
     */
    class RegisteredRegion
    {
    public:
        explicit RegisteredRegion(std::shared_ptr<MemoryRegion> memoryRegion, Region reg, std::uint64_t mrOffset = 0)
            : _mr(std::move(memoryRegion))
            , _region(std::move(reg))
            , _mrOffset(mrOffset)
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
        std::shared_ptr<MemoryRegion> _mr;
        Region _region;
        std::uint64_t _mrOffset;
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
