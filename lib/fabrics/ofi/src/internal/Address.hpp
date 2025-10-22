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
    class FabricAddress final
    {
    public:
        /**
         * Default is an empty fabric address
         */
        FabricAddress() = default;

        /**
         * Retreive the fabric address of an endpoint by passing its fid.
         */
        static FabricAddress fromFid(::fid_t fid);

        /**
         * Convert the raw fabric address into a base64 encoded string.
         */
        [[nodiscard]]
        std::string toBase64() const;

        /**
         * Parse a fabric address from a base64 encoded string
         */
        static FabricAddress fromBase64(std::string_view data);

        /**
         * Pointer to the raw address data.
         */
        void* raw() noexcept;

        /**
         * Pointer to the raw address data.
         */
        [[nodiscard]]
        void const* raw() const noexcept;

        /**
         * Byte-length of the raw address data.
         */
        [[nodiscard]]
        std::size_t size() const noexcept;

        bool operator==(FabricAddress const& other) const noexcept
        {
            return _inner == other._inner;
        }

    private:
        explicit FabricAddress(std::vector<std::uint8_t> addr);
        static FabricAddress retrieveFabricAddress(::fid_t);

    private:
        std::vector<std::uint8_t> _inner;
    };

}
