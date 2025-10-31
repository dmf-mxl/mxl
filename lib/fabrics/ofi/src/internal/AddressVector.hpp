// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include "Address.hpp"
#include "Domain.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief RAII Wrapper to a libfabric `fi_av` instance
     */
    class AddressVector
    {
    public:
        /** \brief Configuration attributes of the AddressVector
         */
        struct Attributes
        {
        public:
            /** \brief Get default attributes
             */
            static Attributes defaults() noexcept;

            /** \brief Convert to libfabric `fi_av_attr` object
             */
            [[nodiscard]]
            ::fi_av_attr toRaw() const noexcept;

        public:
            /** Indicates the expected number of addresses that will be inserted into the AV. The provider uses this to optimize resource allocations.
             */
            std::size_t count;

            /** This field indicates the number of endpoints that will be associated with a specific fabric, or network, address. If the number of
             * endpoints per node is unknown, this value should be set to 0. The provider uses this value to optimize resource allocations. For
             * example, distributed, parallel applications may set this to the number of processes allocated per node, times the number of endpoints
             * each process will open.
             */
            std::size_t epPerNode;
        };

        /** \brief Create an AddressVector
         */
        static std::shared_ptr<AddressVector> open(std::shared_ptr<Domain> domain, Attributes attr = Attributes::defaults());

        ~AddressVector();

        // Copy constructor is deleted
        AddressVector(AddressVector const&) = delete;
        void operator=(AddressVector const&) = delete;

        // Implements proper move semantics. An AddressVector in a moved-from state can no longer be used.
        AddressVector(AddressVector&&) noexcept;
        // Move-assigning an AddressVector to another, releases all resources from the moved-into AddressVector.
        AddressVector& operator=(AddressVector&&);

        /** \brief Insert an address into the address vector. User of this API are responsible for keeping track of inserted addresses to avoid
         * duplicates.
         */
        ::fi_addr_t insert(FabricAddress const& addr);

        /** \brief Remove an address of the address vector.
         */
        void remove(::fi_addr_t addr) noexcept;

        /** \brief Convert a libfabric endpoint address into a human-readable string.
         */
        [[nodiscard]]
        std::string addrToString(FabricAddress const& addr) const;

        /** \brief Mutable accessor of the underlying `fi_av` instance.
         */
        ::fid_av* raw() noexcept;

        /** \brief Immutable accessor of the underlying `fi_av` instance.
         */
        [[nodiscard]]
        ::fid_av const* raw() const noexcept;

    private:
        AddressVector(fid_av* raw, std::shared_ptr<Domain> domain);

        /** \brief Close the address vector and release all resources. Called from the destructor and the move assignment operator.
         */
        void close();

    private:
        fid_av* _raw;
        std::shared_ptr<Domain> _domain;
    };
}
