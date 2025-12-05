// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <rdma/fabric.h>
#include "Provider.hpp"

namespace mxl::lib::fabrics::ofi
{
    template<bool Const>
    class FabricInfoIterator;

    class FabricInfoView;
    class FabricInfoList;

    /** \brief RAII wrapper around a libfabric `fi_info`` structure.
     *
     * Manages the lifetime of a `fi_info` object, ensuring that it is properly
     * deallocated when the FabricInfo object goes out of scope.
     */
    class FabricInfo
    {
    public:
        /** \brief Clone an existing ::fi_info structure, take ownership of the cloned object.
         */
        static FabricInfo clone(::fi_info const*) noexcept;

        /** \brief Own an existing ::fi_info object.
         *
         * The object will be free'd when the FabricInfo object goes out of scope.
         */
        static FabricInfo own(::fi_info*) noexcept;

        /** \brief Create and empty FabricInfo.
         */
        static FabricInfo empty() noexcept;

        ~FabricInfo() noexcept;

        /** \brief Create an owning FabricInfo object from a non-owning view.
         */
        FabricInfo(FabricInfoView) noexcept;

        // Copy constructor and assignment operator.
        FabricInfo(FabricInfo const&) noexcept;
        void operator=(FabricInfo const& other) noexcept;

        // Move constructor and assignment operator.
        FabricInfo(FabricInfo&& other) noexcept;
        FabricInfo& operator=(FabricInfo&&) noexcept;

        /** \brief Dereferencing the FabricInfo return the raw ::fi_info object managed
         * by this object.
         */
        ::fi_info& operator*() noexcept;
        /** \copydoc operator*() */
        ::fi_info const& operator*() const noexcept;
        /** \copydoc operator*() */
        ::fi_info* operator->() noexcept;
        /** \copydoc operator*() */
        ::fi_info const* operator->() const noexcept;

        /** \brief Returns the raw ::fi_info object managed by this FabricInfo instance.
         */
        ::fi_info* raw() noexcept;
        /** \copydoc raw() */
        [[nodiscard]]
        ::fi_info const* raw() const noexcept;

        /** \brief Return a non-owning view of the FabricInfo.
         */
        [[nodiscard]]
        FabricInfoView view() const noexcept;

    private:
        /** \brief Private constructor can only be called by static members. Used to
         * enforce clear ownership semantics.
         */
        explicit FabricInfo(::fi_info*) noexcept;

        /** \brief Called internally to release the ::fi_info object.
         */
        void free() noexcept;

    private:
        ::fi_info* _raw;
    };

    /** \brief Non-owning view of a FabricInfo. Returned when iterating over a FabricInfoList.
     */
    class FabricInfoView
    {
    public:
        /** \brief Dereferencing returns the raw ::fi_info object associated with this view.
         */
        ::fi_info& operator*() noexcept;
        /** \copydoc operator*() */
        ::fi_info const& operator*() const noexcept;
        /** \copydoc operator*() */
        ::fi_info* operator->() noexcept;
        /** \copydoc operator*() */
        ::fi_info const* operator->() const noexcept;

        /** \brief Returns the raw ::fi_info object associated with this view.
         */
        ::fi_info* raw() noexcept;
        /** \copydoc raw() */
        [[nodiscard]]
        ::fi_info const* raw() const noexcept;

        /** \brief Return an owned version that can be moved, copied and dereferenced safely even if
         * the original FabricInfo object has been released.
         */
        FabricInfo owned() noexcept;

    private:
        friend FabricInfoIterator<true>;
        friend FabricInfoIterator<false>;
        friend FabricInfo;

        /// Private constructor.
        FabricInfoView(::fi_info const*);

    private:
        ::fi_info* _raw;
    };

    /**
     * \brief Implements a const/non-const ForwardIterator over a ::fi_info linked list,
     * for use with range-based for-loops, std::algorithms and ranges.
     */
    template<bool Const>
    class FabricInfoIterator
    {
    public:
        friend FabricInfoList;

        // Satisfies ForwardIterator.
        using difference_type = std::ptrdiff_t;
        using value_type = std::conditional_t<Const, FabricInfoView const, FabricInfoView>;
        using raw_type = std::conditional_t<Const, ::fi_info const*, ::fi_info*>;

    public:
        FabricInfoIterator()
            : _it(nullptr)
        {}

        value_type operator*() const
        {
            return value_type{_it};
        }

        FabricInfoIterator& operator++()
        {
            _it = _it->next;
            return *this;
        }

        FabricInfoIterator operator++(int)
        {
            auto current = _it;
            ++*this;
            return FabricInfoIterator{current};
        }

        bool operator==(FabricInfoIterator const& other) const noexcept
        {
            return other._it == _it;
        }

        bool operator!=(FabricInfoIterator const& other) const noexcept
        {
            return !(*this == other);
        }

    private:
        explicit FabricInfoIterator(raw_type it) noexcept
            : _it(it)
        {}

    private:
        raw_type _it;
    };

    /**
     * \brief Wrapper for a linked-list of fi_info objects, mostly returned from
     * ::fi_getinfo.
     *
     * Can be iterated over and used with std::algorithms and std::ranges.
     */
    class FabricInfoList
    {
    public:
        // Type aliases for const and non-const versions of the iterator template
        using iterator = FabricInfoIterator<false>;
        using const_iterator = FabricInfoIterator<true>;

    public:
        /**
         * \brief  Get a list of provider configurations supported to the specified
         * node/service
         */
        static FabricInfoList get(char const* node, char const* service, Provider provider, std::uint64_t caps, ::fi_ep_type epType);

        /** \brief Take ownership over a fi_info raw pointer.
         */
        static FabricInfoList own(::fi_info* info) noexcept;

        /** \brief calls ::fi_freeinfo to deallocate the underlying linked-list
         */
        ~FabricInfoList();

        // deleted copy constuctor
        FabricInfoList(FabricInfoList const&) = delete;
        FabricInfoList& operator=(FabricInfoList const&) = delete;

        // move semantics
        FabricInfoList(FabricInfoList&& other) noexcept;
        FabricInfoList& operator=(FabricInfoList&& other) noexcept;

        // non-const forward iterator
        iterator begin() noexcept;
        iterator end() noexcept;

        // const forward iterator
        [[nodiscard]]
        const_iterator begin() const noexcept;
        [[nodiscard]]
        const_iterator end() const noexcept;

        // const forward iterator
        [[nodiscard]]
        const_iterator cbegin() const noexcept;
        [[nodiscard]]
        const_iterator cend() const noexcept;

    private:
        explicit FabricInfoList(::fi_info*) noexcept;

        void free();

    private:
        ::fi_info* _begin;
    };

}
