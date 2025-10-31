// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <bits/types/struct_iovec.h>

namespace mxl::lib::fabrics::ofi
{
    /** \brief Represent a source memory region used for for data transfer.
     *
     * This can be constructed directly if no memory registration is needed.
     * Otherwise, it can be generated from a `RegisteredRegion`.
     */
    struct LocalRegion
    {
    public:
        // \brief Convert this LocalRegion to a struct iovec used by libfabric transfer functions.
        [[nodiscard]]
        ::iovec toIovec() const noexcept;

    public:
        std::uint64_t addr;
        std::size_t len;
        void* desc;
    };

    /** \brief Represent a scatter-gather list of source memory regions used for data transfer.
     */
    class LocalRegionGroup
    {
    public:
        using iterator = std::vector<LocalRegion>::iterator;
        using const_iterator = std::vector<LocalRegion>::const_iterator;

    public:
        LocalRegionGroup(std::vector<LocalRegion> inner)
            : _inner(std::move(inner))
            , _iovs(iovFromGroup(_inner))
            , _descs(descFromGroup(_inner))
        {}

        /** \brief Return the underlying array of iovec structures representing the memory region group.
         */
        [[nodiscard]]
        ::iovec const* asIovec() const noexcept;

        /** \brief Return the underlying array of descriptors representing the memory region group.
         */
        [[nodiscard]]
        void* const* desc() const noexcept;

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

        LocalRegion& operator[](std::size_t index)
        {
            return _inner[index];
        }

        LocalRegion const& operator[](std::size_t index) const
        {
            return _inner[index];
        }

        [[nodiscard]]
        size_t size() const noexcept
        {
            return _inner.size();
        }

    private:
        /** \brief Generate iovec array from a group of LocalRegion.
         */
        static std::vector<::iovec> iovFromGroup(std::vector<LocalRegion> group) noexcept;

        /** \brief Generate descriptor array from a group of LocalRegion.
         */
        static std::vector<void*> descFromGroup(std::vector<LocalRegion> group) noexcept;

    private:
        std::vector<LocalRegion> _inner;

        std::vector<::iovec> _iovs; /**< cached iovec array */
        std::vector<void*> _descs;  /**< cached descriptor array */
    };

}
