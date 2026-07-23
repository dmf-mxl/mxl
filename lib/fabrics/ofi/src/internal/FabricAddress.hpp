// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <netinet/in.h>
#include "FabricInfo.hpp"
#include "Provider.hpp"

namespace mxl::lib::fabrics::ofi
{
    enum class FabricAddressFormat : std::uint32_t
    {
        Unspec = static_cast<std::uint32_t>(FI_ADDR_UNSPEC), // Unspecified
        Sockaddr = FI_SOCKADDR,                              // Any socket address, including infiniband
        SockaddrIp = FI_SOCKADDR_IP,                         // Any socket address, but infiniband is not valid
        SockaddrIn = FI_SOCKADDR_IN,                         // AF_INET socket address
        SockaddrIn6 = FI_SOCKADDR_IN6,                       // AF_INET6 socket address
        SockaddrIb = FI_SOCKADDR_IB,                         // AF_IB socket address
        Efa = FI_ADDR_EFA,                                   // EFA specific proprietary address format
        String = FI_ADDR_STR,                                // String address in the form of scheme://addr, example: fi_ns://<node>:<service>
    };

    /** \brief Convert from libfabric address format value to FabricAddressFormat
     */
    [[nodiscard]]
    FabricAddressFormat convertAddressFormat(std::uint32_t fiAddrFormat);

    /** \brief Convert from libfabric address format value to FabricAddressFormat
     * Never returns FabricAddressFormat::Unspec, and throws InvalidArgument instead.
     */
    [[nodiscard]]
    FabricAddressFormat mustConvertAddressFormat(std::uint32_t fiAddrFormat);

    /** \brief Local declaration of libfabric's internal `struct ofi_sockaddr_ib`.
     *
     * The public libfabric headers do not expose this type, so we mirror its layout
     * byte-for-byte from libfabric's `include/ofi_net.h` to interpret FI_SOCKADDR_IB
     * addresses exactly like `ofi_straddr`.
     */
    struct OfiSockaddrIb
    {
        unsigned short sib_family; // AF_IB
        std::uint16_t sib_pkey;
        std::uint32_t sib_flowinfo;
        std::uint8_t sib_addr[16];
        std::uint64_t sib_sid;
        std::uint64_t sib_sid_mask;
        std::uint64_t sib_scope_id;
    };

    /** \brief Decoded representation of an FI_ADDR_EFA address.
     *
     * libfabric stores this as a 24-byte blob: a 16-byte IPv6 GID followed by a
     * 16-bit queue-pair number and a 32-bit queue key, all in host byte order.
     */
    struct EfaAddress
    {
        std::uint8_t gid[16];
        std::uint16_t qpn;
        std::uint32_t qkey;
    };

    /** \brief Owning, type-safe representation of a libfabric address.
     *
     * Wraps the native address types that the various `addr_format` values map to.
     * The address bytes are copied out of the source `fi_info` on construction, so a
     * FabricAddress remains valid independently of the FabricInfoView it was built from.
     *
     * An empty / unsupported / unrecognised address is represented by std::monostate.
     */
    class FabricAddress
    {
    public:
        using Native = std::variant<std::monostate, // unknown / null / unsupported
            ::sockaddr_in,                          // FI_SOCKADDR_IN, FI_SOCKADDR(_IP) with AF_INET
            ::sockaddr_in6,                         // FI_SOCKADDR_IN6, FI_SOCKADDR(_IP) with AF_INET6
            OfiSockaddrIb,                          // FI_SOCKADDR_IB
            EfaAddress,                             // FI_ADDR_EFA
            std::string>;                           // FI_ADDR_STR

        /** \brief Build a FabricAddress from the source address of \p info. */
        [[nodiscard]]
        static FabricAddress fromSource(FabricInfoView info) noexcept;

        /** \brief Build a FabricAddress from the destination address of \p info. */
        [[nodiscard]]
        static FabricAddress fromDestination(FabricInfoView info) noexcept;

        /** \brief Decode a raw libfabric address buffer of the given `addr_format`.
         *
         * \param addrFormat One of the libfabric FI_ADDR_* / FI_SOCKADDR* constants.
         * \param addr       Pointer to the address bytes, or nullptr.
         * \param len        Length of the address buffer in bytes.
         */
        [[nodiscard]]
        static FabricAddress decode(FabricAddressFormat addrFormat, void const* addr, std::size_t len);

        FabricAddress() noexcept;

        /** \brief Access the underlying variant. */
        [[nodiscard]]
        Native const& native() const noexcept
        {
            return _native;
        }

        /** \brief True if the address could not be decoded (holds std::monostate). */
        [[nodiscard]]
        bool empty() const noexcept
        {
            return std::holds_alternative<std::monostate>(_native);
        }

        /** \brief Format the address exactly as libfabric's internal `ofi_straddr`.
         *
         * Returns an empty string for an empty / undecodable address (mirroring
         * `ofi_straddr` returning NULL).
         */
        [[nodiscard]]
        std::string toString() const;

        /** \brief Return the host portion of the address as a string.
         *
         * For the socket address types this is the textual IP address; for the IB and EFA types it
         * is the GID. For an FI_ADDR_STR address it is the address string itself, and for an empty
         * address it is the empty string.
         */
        [[nodiscard]]
        std::optional<std::string> node() const;

        /** \brief Return the service/port portion of the address as a string. */
        [[nodiscard]]
        std::optional<std::string> service() const;

        /** \brief Return the libfabric address format constant (FI_SOCKADDR_IN, etc.). */
        [[nodiscard]]
        FabricAddressFormat addressFormat() const noexcept;

        /** \brief Parse a node/service pair into a typed address for the given provider. */
        [[nodiscard]]
        static FabricAddress parse(Provider provider, std::optional<std::string> node, std::optional<std::string> service);

    private:
        explicit FabricAddress(Native native) noexcept;

    private:
        Native _native;
    };
}
