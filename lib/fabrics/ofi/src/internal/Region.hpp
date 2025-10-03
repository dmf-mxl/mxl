// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>
#include <uuid.h>
#include <bits/types/struct_iovec.h>
#include <rdma/fi_domain.h>
#include "internal/FlowData.hpp"
#include "mxl/fabrics.h"

namespace mxl::lib::fabrics::ofi
{
    class Region
    {
    public:
        class Location
        {
        public:
            static Location host() noexcept;
            static Location cuda(int deviceId) noexcept;
            static Location fromAPI(mxlFabricsMemoryRegionLocation loc) noexcept;

            /// Return the device id. For host location 0 is returned.
            [[nodiscard]]
            std::uint64_t id() const noexcept;

            /// Convert the current location to libfabric "iface" representation
            [[nodiscard]]
            ::fi_hmem_iface iface() const noexcept;

            [[nodiscard]]
            std::string toString() const noexcept;

            /// Return true if the memory location is on host.
            [[nodiscard]]
            bool isHost() const noexcept;

        private:
            class Host
            {};

            class Cuda
            {
                Cuda(int deviceId)
                    : _deviceId(deviceId)
                {}
                friend class Location;

                int _deviceId;
            };

            using Inner = std::variant<Host, Cuda>;

        private:
            Location(Inner inner)
                : _inner(inner)
            {}

        private:
            Inner _inner;
        };

        explicit Region(std::uintptr_t base, std::size_t size, Location loc = Location::host()) noexcept
            : base(base)
            , size(size)
            , loc(loc)
            , _iovec(iovecFromRegion(base, size))
        {}

    public:
        [[nodiscard]]
        ::iovec const* asIovec() const noexcept;

        [[nodiscard]]
        ::iovec toIovec() const noexcept;

    public:
        std::uintptr_t base;
        std::size_t size;
        Location loc;

    private:
        static ::iovec iovecFromRegion(std::uintptr_t, std::size_t) noexcept;

    private:
        ::iovec _iovec;
    };

    class RegionGroup
    {
    public:
        using iterator = std::vector<Region>::iterator;
        using const_iterator = std::vector<Region>::const_iterator;

    public:
        explicit RegionGroup() = default;

        explicit RegionGroup(std::vector<Region> inner)
            : _inner(std::move(inner))
            , _iovecs(iovecsFromGroup(_inner))
        {}

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

        Region& operator[](std::size_t index)
        {
            return _inner[index];
        }

        Region const& operator[](std::size_t index) const
        {
            return _inner[index];
        }

        [[nodiscard]]
        size_t size() const noexcept
        {
            return _inner.size();
        }

        [[nodiscard]]
        ::iovec const* asIovec() const noexcept;

    private:
        static std::vector<::iovec> iovecsFromGroup(std::vector<Region> const& group) noexcept;

    private:
        std::vector<Region> _inner;
        std::vector<::iovec> _iovecs;
    };

    class RegionGroups
    {
    public:
        using iterator = std::vector<RegionGroup>::iterator;
        using const_iterator = std::vector<RegionGroup>::const_iterator;

    public:
        explicit RegionGroups(std::vector<RegionGroup> inner)
            : _inner(std::move(inner))
        {}

        static RegionGroups* fromAPI(mxlRegions) noexcept;
        [[nodiscard]]
        mxlRegions toAPI() noexcept;

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

        RegionGroup& operator[](std::size_t index)
        {
            return _inner[index];
        }

        RegionGroup const& operator[](std::size_t index) const
        {
            return _inner[index];
        }

        [[nodiscard]]
        size_t size() const noexcept
        {
            return _inner.size();
        }

    private:
        std::vector<RegionGroup> _inner;
    };

    RegionGroups regionGroupsfromFlow(FlowData& flow);
    RegionGroups regionGroupsfromGroups(mxlFabricsMemoryRegionGroup const* groups, size_t count);

}
