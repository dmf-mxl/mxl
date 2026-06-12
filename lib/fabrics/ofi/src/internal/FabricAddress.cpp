// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "FabricAddress.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <endian.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fmt/format.h>
#include "Exception.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        FabricAddress::Native parseInternetAddress(std::string const& ipString, std::uint16_t port = 0)
        {
            if (std::ranges::find(ipString, ':') != ipString.end())
            {
                auto addr = ::in6_addr{};
                if (!::inet_pton(AF_INET6, ipString.c_str(), &addr))
                {
                    throw Exception::invalidArgument("invalid node address");
                }
                return FabricAddress::Native{
                    ::sockaddr_in6{.sin6_family = AF_INET6, .sin6_port = port, .sin6_flowinfo = 0, .sin6_addr = addr, .sin6_scope_id = 0}
                };
            }
            else
            {
                auto addr = ::in_addr{};
                if (!::inet_pton(AF_INET, ipString.c_str(), &addr))
                {
                    throw Exception::invalidArgument("invalid node address");
                }
                return FabricAddress::Native{
                    ::sockaddr_in{.sin_family = AF_INET, .sin_port = port, .sin_addr = addr, .sin_zero = {}}
                };
            }
        }

        /** \brief Wrap `inet_ntop`, returning std::nullopt on failure (mirrors `ofi_straddr`'s `return NULL`). */
        std::optional<std::string> ipToString(int family, void const* addr)
        {
            char str[INET6_ADDRSTRLEN] = {};
            if (!::inet_ntop(family, addr, str, sizeof(str)))
            {
                return std::nullopt;
            }
            return std::string{str};
        }

        /// fi_sockaddr_in://%s:%u
        std::optional<std::string> toString(::sockaddr_in const& sin)
        {
            auto const ip = ipToString(sin.sin_family, &sin.sin_addr);
            if (!ip)
            {
                return std::nullopt;
            }
            return fmt::format("fi_sockaddr_in://{}:{}", *ip, ntohs(sin.sin_port));
        }

        /// fi_sockaddr_in6://[%s]:%u
        std::optional<std::string> toString(::sockaddr_in6 const& sin6)
        {
            auto const ip = ipToString(sin6.sin6_family, &sin6.sin6_addr);
            if (!ip)
            {
                return std::nullopt;
            }
            return fmt::format("fi_sockaddr_in6://[{}]:{}", *ip, ntohs(sin6.sin6_port));
        }

        /// fi_sockaddr_ib://[%s]:0x%x:0x%x:0x%x  (GID : P_Key : port space : Scope ID)
        std::optional<std::string> toString(OfiSockaddrIb const& sib)
        {
            auto const gid = ipToString(AF_INET6, sib.sib_addr);
            if (!gid)
            {
                return std::nullopt;
            }
            return fmt::format("fi_sockaddr_ib://[{}]:0x{:x}:0x{:x}:0x{:x}",
                *gid,                                                             // GID
                ntohs(sib.sib_pkey),                                              // P_Key
                static_cast<std::uint16_t>((be64toh(sib.sib_sid) >> 16) & 0xfff), // port space
                static_cast<std::uint8_t>(be64toh(sib.sib_scope_id) & 0xff));     // Scope ID
        }

        /// fi_addr_efa://[%s]:%u:%u  (GID : QPN : QKey)
        std::optional<std::string> toString(EfaAddress const& efa)
        {
            auto const gid = ipToString(AF_INET6, efa.gid);
            if (!gid)
            {
                return std::nullopt;
            }
            return fmt::format("fi_addr_efa://[{}]:{}:{}", *gid, efa.qpn, efa.qkey);
        }

        /// The address is itself a null-terminated string.
        std::optional<std::string> toString(std::string const& str)
        {
            return str;
        }

        /// Host portion of each address type: the textual IP for sockets, the GID for IB / EFA.
        std::optional<std::string> hostString(::sockaddr_in const& sin)
        {
            return ipToString(sin.sin_family, &sin.sin_addr);
        }

        std::optional<std::string> hostString(::sockaddr_in6 const& sin6)
        {
            return ipToString(sin6.sin6_family, &sin6.sin6_addr);
        }

        std::optional<std::string> hostString(OfiSockaddrIb const& sib)
        {
            return ipToString(AF_INET6, sib.sib_addr);
        }

        std::optional<std::string> hostString(EfaAddress const& efa)
        {
            return ipToString(AF_INET6, efa.gid);
        }

        std::optional<std::string> hostString(std::string const& str)
        {
            return str.substr(0, str.find(':'));
        }

        std::optional<std::string> serviceString(::sockaddr_in const& sin)
        {
            return sin.sin_port == 0 ? std::nullopt : std::make_optional(std::to_string(sin.sin_port));
        }

        std::optional<std::string> serviceString(::sockaddr_in6 const& sin)
        {
            return sin.sin6_port == 0 ? std::nullopt : std::make_optional(std::to_string(sin.sin6_port));
        }

        std::optional<std::string> serviceString(OfiSockaddrIb)
        {
            return std::nullopt;
        }

        std::optional<std::string> serviceString(EfaAddress)
        {
            return std::nullopt;
        }

        std::optional<std::string> serviceString(std::string const& str)
        {
            auto const pos = str.find(':') + 1;
            if (pos >= str.size())
            {
                return std::nullopt;
            }
            return str.substr(pos);
        }
    }

    FabricAddress FabricAddress::fromSource(FabricInfoView info) noexcept
    {
        return decode(info->addr_format, info->src_addr, info->src_addrlen);
    }

    FabricAddress FabricAddress::fromDestination(FabricInfoView info) noexcept
    {
        return decode(info->addr_format, info->dest_addr, info->dest_addrlen);
    }

    FabricAddress FabricAddress::decode(std::uint32_t addrFormat, void const* addr, std::size_t len) noexcept
    {
        // Mirror ofi_straddr: nothing to decode without an address.
        if (!addr)
        {
            return FabricAddress{};
        }

        auto const sockAddrFamily = [&]
        {
            return static_cast<::sockaddr const*>(addr)->sa_family;
        };

        switch (addrFormat)
        {
            case FI_SOCKADDR:
                // Dispatch on the embedded address family (IPv4, IPv6 or IB).
                switch (sockAddrFamily())
                {
                    case AF_INET:  return decode(FI_SOCKADDR_IN, addr, len);
                    case AF_INET6: return decode(FI_SOCKADDR_IN6, addr, len);
                    case AF_IB:    return decode(FI_SOCKADDR_IB, addr, len);
                    default:       return FabricAddress{};
                }
            case FI_SOCKADDR_IP:
                // Like FI_SOCKADDR, but IB is not a valid family here.
                switch (sockAddrFamily())
                {
                    case AF_INET:  return decode(FI_SOCKADDR_IN, addr, len);
                    case AF_INET6: return decode(FI_SOCKADDR_IN6, addr, len);
                    default:       return FabricAddress{};
                }
            case FI_SOCKADDR_IN:
            {
                if (len < sizeof(::sockaddr_in))
                {
                    return FabricAddress{};
                }
                ::sockaddr_in sin;
                std::memcpy(&sin, addr, sizeof(sin));
                return FabricAddress{sin};
            }
            case FI_SOCKADDR_IN6:
            {
                if (len < sizeof(::sockaddr_in6))
                {
                    return FabricAddress{};
                }
                ::sockaddr_in6 sin6;
                std::memcpy(&sin6, addr, sizeof(sin6));
                return FabricAddress{sin6};
            }
            case FI_SOCKADDR_IB:
            {
                if (len < sizeof(OfiSockaddrIb))
                {
                    return FabricAddress{};
                }
                OfiSockaddrIb sib;
                std::memcpy(&sib, addr, sizeof(sib));
                return FabricAddress{sib};
            }
            case FI_ADDR_EFA:
            {
                // 16-byte IPv6 GID, 16-bit QPN at offset 16, 32-bit QKey at offset 20.
                constexpr auto efaAddrLen = std::size_t{24};
                if (len < efaAddrLen)
                {
                    return FabricAddress{};
                }
                auto const* bytes = static_cast<std::uint8_t const*>(addr);
                auto efa = EfaAddress{};
                std::memcpy(efa.gid, bytes, sizeof(efa.gid));
                std::memcpy(&efa.qpn, bytes + 16, sizeof(efa.qpn));
                std::memcpy(&efa.qkey, bytes + 20, sizeof(efa.qkey));
                return FabricAddress{efa};
            }
            case FI_ADDR_STR: return FabricAddress{std::string{static_cast<char const*>(addr)}};
            default:          return FabricAddress{};
        }
    }

    std::string FabricAddress::toString() const
    {
        return std::visit(
            [](auto const& value) -> std::string
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    return "";
                }
                else
                {
                    return ofi::toString(value).value_or("");
                }
            },
            _native);
    }

    std::optional<std::string> FabricAddress::node() const
    {
        return std::visit(
            [](auto const& value) -> std::optional<std::string>
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    return std::nullopt;
                }
                else
                {
                    return ofi::hostString(value);
                }
            },
            _native);
    }

    std::optional<std::string> FabricAddress::service() const
    {
        return std::visit(
            [](auto const& value) -> std::optional<std::string>
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    return std::nullopt;
                }
                else
                {
                    return ofi::serviceString(value);
                }
            },
            _native);
    }

    std::uint32_t FabricAddress::addressFormat() const noexcept
    {
        return std::visit(
            overloaded{
                [](std::monostate const&) { return FI_FORMAT_UNSPEC; },
                [](::sockaddr_in const&) { return FI_SOCKADDR_IN; },
                [](::sockaddr_in6 const&) { return FI_SOCKADDR_IN6; },
                [](OfiSockaddrIb const&) { return FI_SOCKADDR_IB; },
                [](EfaAddress const&) { return FI_ADDR_EFA; },
                [](std::string const&) { return FI_ADDR_STR; },
            },
            _native);
    }

    FabricAddress FabricAddress::parse(Provider provider, std::optional<std::string> node, std::optional<std::string> service)
    {
        if (!node)
        {
            if (service)
            {
                throw Exception::invalidArgument("node address is required when service address is specified");
            }

            return FabricAddress{};
        }

        switch (provider)
        {
            // SHM provider always expected FI_ADDR_STR
            case Provider::SHM:
            {
                if (service && !service->empty())
                {
                    return FabricAddress{*node + ":" + *service};
                }
                else
                {
                    return FabricAddress{*node};
                }
            };
            // For TCP and Verbs we only support FI_SOCKADDR_IN and FI_SOCKADDR_IN6
            case Provider::TCP: [[fallthrough]];
            case Provider::VERBS:
            {
                if (service && !service->empty())
                {
                    auto port = std::stoi(*service);
                    return FabricAddress{parseInternetAddress(*node, port)};
                }
                return FabricAddress{parseInternetAddress(*node)};
            }
            // EFA only supports FI_ADDR_EFA
            case Provider::EFA:
            {
                auto parsed = parseInternetAddress(*node);
                auto sin6 = std::get_if<::sockaddr_in6>(&parsed);
                if (!sin6)
                {
                    throw Exception::invalidArgument("invalid address format for EFA provider");
                }

                auto addr = EfaAddress{.gid = {}, .qpn = 0, .qkey = 0};
                std::memcpy(addr.gid, &sin6->sin6_addr, sizeof(addr.gid));
                return FabricAddress{addr};
            }
            default: throw Exception::invalidState("must specify provider");
        }
    }
}
