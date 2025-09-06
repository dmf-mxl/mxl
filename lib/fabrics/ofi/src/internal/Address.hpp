// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>
#include <rfl.hpp>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <sys/types.h>

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
        explicit FabricAddress(std::vector<uint8_t> addr);
        static FabricAddress retrieveFabricAddress(::fid_t);

        std::vector<uint8_t> _inner;
    };

    struct FabricAddressRfl
    {
        rfl::Field<"addr", std::string> addr;

        static FabricAddressRfl from_class(FabricAddress const& fa)
        {
            return FabricAddressRfl{.addr = fa.toBase64()};
        }

        [[nodiscard]]
        FabricAddress to_class() const
        {
            return FabricAddress::fromBase64(addr.get());
        }
    };
}

namespace rfl::parsing
{
    namespace ofi = mxl::lib::fabrics::ofi;

    template<class ReaderType, class WriterType, class ProcessorsType>
    struct Parser<ReaderType, WriterType, ofi::FabricAddress, ProcessorsType>
        : public rfl::parsing::CustomParser<ReaderType, WriterType, ProcessorsType, ofi::FabricAddress, ofi::FabricAddressRfl>
    {};
}
