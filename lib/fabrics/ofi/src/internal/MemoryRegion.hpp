// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/** \file MemoryRegion.hpp
 * \brief Wrapper for libfabric memory region (fid_mr) - registered memory for RDMA operations.
 *
 * MemoryRegion represents a memory buffer that has been registered (pinned) for RDMA operations.
 * Memory registration is required for zero-copy RDMA transfers.
 *
 * What memory registration does:
 * - Pins pages in physical memory (prevents swapping to disk)
 * - Creates DMA mapping for NIC access
 * - Returns memory descriptor (desc) for local operations
 * - Returns remote key (rkey) for remote peer access
 *
 * Key outputs from registration:
 * - desc: Memory descriptor for local side (used in LocalRegion)
 * - rkey: Remote protection key for remote side (used in RemoteRegion)
 *
 * Access flags (combined with bitwise OR):
 * - FI_SEND/FI_RECV: Message operations
 * - FI_WRITE: Local buffer for RDMA writes (initiator)
 * - FI_REMOTE_READ: Remote can read from this buffer (target)
 * - FI_REMOTE_WRITE: Remote can write to this buffer (target)
 *
 * Typical workflow:
 * 1. Create Region (base pointer + size)
 * 2. Register with MemoryRegion::reg(domain, region, access)
 * 3. Extract desc for LocalRegion, rkey for RemoteRegion
 * 4. Send rkey to remote peer (out-of-band or via immediate data)
 * 5. Use in RDMA operations
 */

#pragma once

#include <cstdint>
#include <bits/types/struct_iovec.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include "Domain.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief RAII wrapper around a libfabric memory region (`fid_mr`).
     *
     * MemoryRegion manages the lifecycle of a registered memory buffer.
     * Automatically unregisters (fi_close) in destructor.
     */
    class MemoryRegion
    {
    public:
        /** \brief Register a memory region with a specified domain and access permissions.
         *
         * \param domain The domain to register the memory region with.
         * \param region The region to register.
         * \param access The access permissions for the memory region. Possible values are:
         *  - FI_SEND: The memory buffer may be used in outgoing message data transfers.
         *  - FI_RECV: The memory buffer may be used to receive inbound message transfers.
         *  - FI_WRITE: The memory buffer may be used as the source buffer for RMA write and atomic operations on the initiator side.
         *  - FI_REMOTE_READ: The memory buffer may be used as the source buffer of an RMA read operation on the target side.
         *  - FI_REMOTE_WRITE: The memory buffer may be used as the target buffer of an RMA write operation on the target side.
         *
         * \return A MemoryRegion object representing the registered memory region.
         */
        static MemoryRegion reg(Domain& domain, Region const& region, std::uint64_t access);

        ~MemoryRegion();

        MemoryRegion(MemoryRegion const&) = delete;
        void operator=(MemoryRegion const&) = delete;

        MemoryRegion(MemoryRegion&&) noexcept;
        MemoryRegion& operator=(MemoryRegion&&) noexcept;

        /** \brief Access the underlying raw `fid_mr` pointer.
         */
        [[nodiscard]]
        ::fid_mr* raw() noexcept;
        /** \copydoc raw() */
        [[nodiscard]]
        ::fid_mr const* raw() const noexcept;

        /** \brief Get the descriptor associated with the memory region.
         */
        [[nodiscard]]
        void* desc() const noexcept;

        /** \brief Get the remote key (rkey) associated with the memory region.
         *
         * Thisthe mechanism for protecting a registered region. One should be careful and should only share this secret with initiators that are
         * allowed to access this memory region.
         */
        [[nodiscard]]
        std::uint64_t rkey() const noexcept;

    private:
        friend class RegisteredRegion;

    private:
        /** \brief Internal method to release the memory region.
         */
        void close();

        MemoryRegion(::fid_mr* raw);

    private:
        ::fid_mr* _raw;
    };
}
