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
#include <mxl-internal/FlowData.hpp>
#include <rdma/fi_domain.h>
#include "mxl/fabrics.h"

namespace mxl::lib::fabrics::ofi
{

    /** \brief Represent a memory region (unregistered).
     */
    class Region
    {
    public:
        /** \brief Represent the location of a memory region.
         */
        class Location
        {
        public:
            /** \brief specify a host memory location.
             */
            static Location host() noexcept;
            /** \brief specify a CUDA device memory location.
             *
             * \param deviceId The CUDA device id.
             */
            static Location cuda(int deviceId) noexcept;
            /** \brief Convert between external and internal versions of this type
             */
            static Location fromAPI(mxlFabricsMemoryRegionLocation loc);

            /** \brief Return the device id. For host location 0 is returned.
             */
            [[nodiscard]]
            std::uint64_t id() const noexcept;

            /** \brief Convert the current location to libfabric "iface" representation
             */
            [[nodiscard]]
            ::fi_hmem_iface iface() const noexcept;

            /** \brief Generate a string representation of the memory location.
             */
            [[nodiscard]]
            std::string toString() const noexcept;

            /** \brief Return true if the memory location is on host.
             */
            [[nodiscard]]
            bool isHost() const noexcept;

        private:
            /** \brief Represent a host memory location variant.
             */
            class Host
            {};

            /** \brief Represent a CUDA device memory location variant.
             */
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

        /** \brief Construct a Region object.
         *
         * \param base The base address of the memory region.
         * \param size The size of the memory region in bytes.
         * \param loc The location of the memory region \see Location.
         */
        explicit Region(std::uintptr_t base, std::size_t size, Location loc = Location::host()) noexcept
            : base(base)
            , size(size)
            , loc(loc)
            , _iovec(iovecFromRegion(base, size))
        {}

    public:
        /** \brief Return the underlying iovec structure representing the memory region.
         */
        [[nodiscard]]
        ::iovec const* asIovec() const noexcept;

        /** \brief Convert the Region to an iovec structure.
         */
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

    /** \brief Represent a group of memory regions.
     */
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

    /** \brief Represent a collection of memory regions.
     *
     * This is the internal strucure representing mxlRegions API type.
     */
    class MxlRegions
    {
    public:
        /** \brief Convert between external and internal versions of this type
         */
        static MxlRegions* fromAPI(mxlRegions) noexcept;
        /** \copydoc fromAPI() */
        [[nodiscard]]
        mxlRegions toAPI() noexcept;

        /** \brief View accessor for the underlying regions.
         */
        [[nodiscard]]
        std::vector<Region> const& regions() const noexcept;

        // [[nodiscard]]
        // DataLayout const& dataLayout() const noexcept;

    private:
        friend MxlRegions mxlRegionsFromFlow(FlowData const& flow);
        friend MxlRegions mxlRegionsFromUser(mxlFabricsMemoryRegion const* regions, size_t count);

    private:
        MxlRegions(std::vector<Region> regions /*,  DataLayout dataLayout*/)
            : _regions(std::move(regions))
        // , _layout(std::move(dataLayout))
        {}

    private:
        std::vector<Region> _regions;
        // DataLayout _layout;
    };

    /** \brief Convert a FlowData's memory regions to MxlRegions.
     *
     * FlowData are obtained from an MXL FlowWriter or FlowReader.
     */
    MxlRegions mxlRegionsFromFlow(FlowData const& flow);

    /** \brief Convert user-provided memory regions to MxlRegions.
     *
     * Used to convert mxlFabricsMemoryRegion arrays provided by the user.
     */
    MxlRegions mxlRegionsFromUser(mxlFabricsMemoryRegion const* regions, size_t count);
}
