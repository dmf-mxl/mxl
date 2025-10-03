// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <rfl.hpp>
#include <rdma/fi_rma.h>
#include <rfl/Rename.hpp>

namespace mxl::lib::fabrics::ofi
{

    namespace rfl_types
    {
        struct RemoteRegionGroupRfl;
    }

    class LocalRegionGroup;

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
        friend struct rfl_types::RemoteRegionGroupRfl;

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

    namespace rfl_types
    {
        struct RemoteRegionRfl
        {
            rfl::Rename<"addr", std::uint64_t> addr;
            rfl::Rename<"len", std::size_t> len;
            rfl::Rename<"rkey", std::uint64_t> rkey;

            static RemoteRegionRfl from_class(RemoteRegion const& reg)
            {
                return RemoteRegionRfl{.addr = reg.addr, .len = reg.len, .rkey = reg.rkey};
            }

            [[nodiscard]]
            RemoteRegion to_class() const
            {
                return {.addr = addr.get(), .len = len.get(), .rkey = rkey.get()};
            }
        };

        struct RemoteRegionGroupRfl
        {
            rfl::Rename<"group", std::vector<RemoteRegion>> group;

            static RemoteRegionGroupRfl from_class(RemoteRegionGroup const& group)
            {
                return RemoteRegionGroupRfl{.group = group.clone()};
            };

            [[nodiscard]]
            RemoteRegionGroup to_class() const
            {
                return group.get();
            };
        };

    };
}

namespace rfl::parsing
{
    namespace ofi = mxl::lib::fabrics::ofi;

    template<class ReaderType, class WriterType, class ProcessorsType>
    struct Parser<ReaderType, WriterType, ofi::RemoteRegion, ProcessorsType>
        : public rfl::parsing::CustomParser<ReaderType, WriterType, ProcessorsType, ofi::RemoteRegion, ofi::rfl_types::RemoteRegionRfl>
    {};

    template<class ReaderType, class WriterType, class ProcessorsType>
    struct Parser<ReaderType, WriterType, ofi::RemoteRegionGroup, ProcessorsType>
        : public rfl::parsing::CustomParser<ReaderType, WriterType, ProcessorsType, ofi::RemoteRegionGroup, ofi::rfl_types::RemoteRegionGroupRfl>
    {};
}
