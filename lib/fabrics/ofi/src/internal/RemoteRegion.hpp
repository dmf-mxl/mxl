// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/** \file RemoteRegion.hpp
 * \brief Remote memory region descriptor for RDMA target buffers.
 *
 * RemoteRegion represents a remote memory buffer that the initiator can target with RDMA operations.
 * It's sent from the target to the initiator (out-of-band or via control message).
 *
 * Key fields:
 * - addr: Remote address (virtual address or 0-based offset depending on FI_MR_VIRT_ADDR mode)
 * - len: Length of buffer in bytes
 * - rkey: Remote protection key (from fi_mr_key()) - grants access permission to remote buffer
 *
 * RemoteRegion vs LocalRegion:
 * - RemoteRegion: Destination buffer on target (write to there) - contains rkey
 * - LocalRegion: Source buffer on initiator (write from here) - contains desc
 *
 * Security consideration:
 * - rkey is a secret that grants RDMA access to memory region
 * - Only share with trusted peers
 * - In MXL, rkeys may be exchanged via immediate data or separate control channel
 *
 * RemoteRegionGroup:
 * - Collection of remote regions for scatter-gather RDMA to multiple target buffers
 * - Converted to fi_rma_iov array for libfabric RMA operations
 *
 * Generation:
 * - Created from RegisteredRegion (after memory registration on target)
 * - Sent to initiator peer for use in RDMA write operations
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <rdma/fi_rma.h>

namespace mxl::lib::fabrics::ofi
{

    /** \brief Represent a remote memory region used for data transfer.
     *
     * This can be constructed from a `RegisteredRegion` on the target side.
     * Sent to initiator to enable RDMA writes to target memory.
     */
    struct RemoteRegion
    {
    public:
        /** \brief Convert this RemoteRegion to a struct fi_rma_iov used by libfabric RMA transfer functions.
         */
        [[nodiscard]]
        ::fi_rma_iov toRmaIov() const noexcept;

        bool operator==(RemoteRegion const& other) const noexcept;

    public:
        std::uint64_t addr;
        std::size_t len;
        std::uint64_t rkey;
    };

    class RemoteRegionGroup
    {
    public:
        using iterator = std::vector<RemoteRegion>::iterator;
        using const_iterator = std::vector<RemoteRegion>::const_iterator;

    public:
        RemoteRegionGroup(std::vector<RemoteRegion> group)
            : _inner(std::move(group))
            , _rmaIovs(rmaIovsFromGroup(_inner))
        {}

        [[nodiscard]]
        ::fi_rma_iov const* asRmaIovs() const noexcept;

        bool operator==(RemoteRegionGroup const& other) const noexcept;

        iterator begin()
        {
            return _inner.begin();
        }

        iterator end()
        {
            return _inner.end();
        }

        [[nodiscard]]
        const_iterator begin() const
        {
            return _inner.cbegin();
        }

        [[nodiscard]]
        const_iterator end() const
        {
            return _inner.cend();
        }

        RemoteRegion& operator[](std::size_t index)
        {
            return _inner[index];
        }

        RemoteRegion const& operator[](std::size_t index) const
        {
            return _inner[index];
        }

        [[nodiscard]]
        std::size_t size() const noexcept
        {
            return _inner.size();
        }

    private:
        static std::vector<::fi_rma_iov> rmaIovsFromGroup(std::vector<RemoteRegion> group) noexcept;

        [[nodiscard]]
        std::vector<RemoteRegion> clone() const
        {
            return _inner;
        }

    private:
        std::vector<RemoteRegion> _inner;

        std::vector<::fi_rma_iov> _rmaIovs;
    };
}
