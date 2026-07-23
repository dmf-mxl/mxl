// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include "FabricAddress.hpp"

namespace mxl::lib::fabrics::ofi
{
    /**
     * \brief Wrapper around a libfabric endpoint address.
     */
    class RawFabricAddress final
    {
    public:
        /**
         * \brief Default is an empty fabric address
         */
        RawFabricAddress();

        /**
         * \brief Retrieve the fabric address of an endpoint by passing its fid.
         * \param fid libfabric address object
         * \param addressFormat A libfabric address format like FI_SOCKADDR_IN, FI_ADDR_EFA, etc..
         */
        [[nodiscard]]
        static RawFabricAddress fromFid(::fid_t fid, FabricInfoView info);

        /**
         * \brief Convert the raw fabric address into a base64 encoded string.
         */
        [[nodiscard]]
        std::string toBase64() const;

        /**
         * \brief Parse a fabric address from a base64 encoded string
         * \param data Base64 encoded raw binary address
         * \param addressFormat A libfabric address format like FI_SOCKADDR_IN, FI_ADDR_EFA, etc..
         */
        [[nodiscard]]
        static RawFabricAddress fromBase64(std::string_view data, FabricAddressFormat addressFormat);

        /**
         * \brief Pointer to the raw address data.
         */
        [[nodiscard]]
        void* raw() noexcept;

        /**
         * \brief Pointer to the raw address data.
         */
        [[nodiscard]]
        void const* raw() const noexcept;

        /**
         * \brief Byte-length of the raw address data.
         */
        [[nodiscard]]
        std::size_t size() const noexcept;

        /**
         * \brief Return the fabric specific libfaric address format, or FI_FORMAT_UNSPEC if empty.
         */
        [[nodiscard]]
        FabricAddressFormat format() const noexcept;

        /**
         * \brief Decode into a strongly typed FabricAddress.
         */
        [[nodiscard]]
        FabricAddress decode() const;

        bool operator==(RawFabricAddress const& other) const noexcept;

    private:
        explicit RawFabricAddress(std::vector<std::uint8_t> addr, FabricAddressFormat addressFormat);

        /** \brief Retrieve the fabric address of an endpoint by using its fid.
         */
        [[nodiscard]]
        static RawFabricAddress retrieveFabricAddress(::fid_t fid, FabricInfoView info);

    private:
        std::vector<std::uint8_t> _inner; /**< libfabric address represented as a buffer of bytes */
        FabricAddressFormat _addressFormat;
    };

}
