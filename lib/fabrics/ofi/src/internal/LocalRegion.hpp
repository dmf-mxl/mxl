// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <sys/uio.h>

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
        /** \brief Create a sub-region of this LocalRegion.
         *
         * \param offset The offset within this region where the sub-region starts.
         * \param length The length of the sub-region.
         * \return A new LocalRegion representing the specified sub-region.
         */
        [[nodiscard]]
        LocalRegion sub(std::uint64_t offset, std::size_t length) const;

        /** \brief Convert this LocalRegion to a struct iovec used by libfabric transfer functions.
         */
        [[nodiscard]]
        ::iovec toIovec() const noexcept;

    public:
        std::uint64_t addr;
        std::size_t len;
        void* desc;
    };

    class LocalRegionGroupSpan; // Forward declaration

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
        std::size_t size() const noexcept
        {
            return _inner.size();
        }

        /** \brief Return a span representing a contiguous subset of the LocalRegionGroup.
         *
         * \param beginIndex The starting index of the span (inclusive).
         * \param endIndex The ending index of the span (exclusive).
         * \return A LocalRegionGroupSpan representing the specified subset of the LocalRegionGroup.
         */
        [[nodiscard]]
        LocalRegionGroupSpan span(std::size_t beginIndex, std::size_t endIndex) const;

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

    /** \brief Represent a span of a LocalRegionGroup.
     *
     * This is used to represent a contiguous subset of a LocalRegionGroup.
     */
    class LocalRegionGroupSpan
    {
        friend class LocalRegionGroup;

    public:
        [[nodiscard]]
        ::iovec const* asIovec() const noexcept
        {
            return _iovec.data();
        }

        [[nodiscard]]
        void* const* desc() const noexcept
        {
            return _descs.data();
        }

        [[nodiscard]]
        std::size_t size() const noexcept
        {
            return _inner.size();
        }

        /** \brief Returns the sum of each region size in the span.
         */
        [[nodiscard]]
        std::size_t byteSize() const noexcept;

    private:
        LocalRegionGroupSpan(std::span<LocalRegion const>, std::span<::iovec const>, std::span<void* const>);

    private:
        std::span<LocalRegion const> _inner;
        std::span<::iovec const> _iovec;
        std::span<void* const> _descs;
    };
}
