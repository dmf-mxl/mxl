// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <rdma/fi_rma.h>

namespace mxl::lib::fabrics::ofi
{
    struct RemoteRegion
    {
    public:
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
