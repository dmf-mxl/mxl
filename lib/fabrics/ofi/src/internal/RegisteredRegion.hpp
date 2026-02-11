// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/** \file RegisteredRegion.hpp
 * \brief Registered memory region - combines original Region with MemoryRegion after fi_mr_reg().
 *
 * RegisteredRegion ties together:
 * - Original Region (base address, size, location)
 * - MemoryRegion (fid_mr handle from fi_mr_reg() containing desc and rkey)
 *
 * After registration, RegisteredRegion can generate:
 * - LocalRegion: For local RDMA operations (contains desc for local DMA)
 * - RemoteRegion: For remote peer (contains rkey for remote access, address in correct mode)
 *
 * Address modes:
 * - Virtual addressing: Remote uses actual pointer value (region.base)
 * - Offset addressing: Remote uses 0-based offset (0)
 * - Mode determined by FI_MR_VIRT_ADDR flag in domain attributes
 *
 * Workflow:
 * 1. Create Region (unregistered memory descriptor)
 * 2. Register with MemoryRegion::reg() → gets fid_mr handle
 * 3. Wrap in RegisteredRegion
 * 4. Call toLocal() for initiator-side LocalRegion
 * 5. Call toRemote() for target-side RemoteRegion (send to peer)
 */

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
     *
     * Combines original Region with MemoryRegion handle to generate LocalRegion and RemoteRegion.
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
