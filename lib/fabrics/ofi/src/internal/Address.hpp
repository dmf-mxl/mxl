// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>

namespace mxl::lib::fabrics::ofi
{
    /**
     * \brief Wrapper around a libfabric endpoint address.
     */
    class FabricAddress final
    {
    public:
        /**
         * \brief Default is an empty fabric address
         */
        FabricAddress() = default;

        /**
         * \brief Retrieve the fabric address of an endpoint by passing its fid.
         */
        static FabricAddress fromFid(::fid_t fid);

        /**
         * \brief Convert the raw fabric address into a base64 encoded string.
         */
        [[nodiscard]]
        std::string toBase64() const;

        /**
         * \brief Parse a fabric address from a base64 encoded string
         */
        static FabricAddress fromBase64(std::string_view data);

        /**
         * \brief Pointer to the raw address data.
         */
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

        bool operator==(FabricAddress const& other) const noexcept;

    private:
        explicit FabricAddress(std::vector<std::uint8_t> addr);

        /** \brief Retrieve the fabric address of an endpoint by using its fid.
         */
        static FabricAddress retrieveFabricAddress(::fid_t);

    private:
        std::vector<std::uint8_t> _inner; /**< libfabric address represented as a buffer of bytes */
    };

}
