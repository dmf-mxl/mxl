// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <rdma/fi_rma.h>

namespace mxl::lib::fabrics::ofi
{

    /** \brief Represent a remote memory region used for data transfer.
     *
     * This can be constructed from a `RegisteredRegion`.
     */
    struct RemoteRegion
    {
    public:
        /** \brief Create a sub-region of this RemoteRegion.
         *
         * \param offset The offset within this region where the sub-region starts.
         * \param length The length of the sub-region.
         * \return A new RemoteRegion representing the specified sub-region.
         */
        [[nodiscard]]
        RemoteRegion sub(std::uint64_t offset, std::size_t length) const;

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

    /** \brief Represent a group of remote memory regions used for data transfer.
     */
    class RemoteRegionGroup
    {
    public:
        using iterator = std::vector<RemoteRegion>::iterator;
        using const_iterator = std::vector<RemoteRegion>::const_iterator;

    public:
        /** \brief Convert a vector of RemoteRegion into a RemoteRegionGroup
         */
        RemoteRegionGroup(std::vector<RemoteRegion> const& group)
            : _inner(std::move(group))
            , _rmaIovs(rmaIovsFromGroup(group.begin(), group.end()))
        {}

        /** \brief Get a pointer to an array of fi_rma_iov structures representing the remote regions.
         *
         * The returned pointer is valid as long as this RemoteRegionGroup object is alive.
         */
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
        /** \brief Convert a vector of RemoteRegion into a vector of fi_rma_iov structures. Used by the constructor.
         */
        template<typename It>
        [[nodiscard]]
        static std::vector<::fi_rma_iov> rmaIovsFromGroup(It&& begin, It&& end) noexcept
        {
            std::vector<::fi_rma_iov> rmaIovs;
            std::ranges::transform(
                std::forward<It>(begin), std::forward<It>(end), std::back_inserter(rmaIovs), [](RemoteRegion const& reg) { return reg.toRmaIov(); });
            return rmaIovs;
        }

    private:
        std::vector<RemoteRegion> _inner; /**< The underlying remote regions */

        std::vector<::fi_rma_iov>
            _rmaIovs; /**< An alternative representation of the remote regions as fi_rma_iov structures. Must be kept synchronized with _inner. */
    };
}
