// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstring>
#include <array>
#include <variant>
#include <vector>
#include <endian.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <netinet/in.h>
#include "Address.hpp"
#include "FabricAddress.hpp"

using namespace mxl::lib::fabrics::ofi;

namespace
{
    /// Build a native sockaddr_in, exactly as libfabric would hand it to FabricAddress::decode.
    [[nodiscard]]
    ::sockaddr_in makeSockaddrIn(char const* ip, std::uint16_t port)
    {
        auto sin = ::sockaddr_in{};
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        REQUIRE(::inet_pton(AF_INET, ip, &sin.sin_addr) == 1);
        return sin;
    }

    [[nodiscard]]
    ::sockaddr_in6 makeSockaddrIn6(char const* ip, std::uint16_t port)
    {
        auto sin6 = ::sockaddr_in6{};
        sin6.sin6_family = AF_INET6;
        sin6.sin6_port = htons(port);
        REQUIRE(::inet_pton(AF_INET6, ip, &sin6.sin6_addr) == 1);
        return sin6;
    }

    /// Build a native ofi_sockaddr_ib. The service id packs the port space in bits 16..31 and
    /// the port in bits 0..15; the P_Key, scope id and service id are stored in network order.
    [[nodiscard]]
    OfiSockaddrIb makeSockaddrIb(char const* gid, std::uint16_t pkey, std::uint16_t portSpace, std::uint64_t scopeId, std::uint16_t port)
    {
        auto sib = OfiSockaddrIb{};
        sib.sib_family = AF_IB;
        sib.sib_pkey = htons(pkey);
        REQUIRE(::inet_pton(AF_INET6, gid, sib.sib_addr) == 1);
        sib.sib_sid = htobe64((static_cast<std::uint64_t>(portSpace) << 16) | port);
        sib.sib_scope_id = htobe64(scopeId);
        return sib;
    }

    /// Build the 24-byte FI_ADDR_EFA blob: a 16-byte IPv6 GID, a 16-bit QPN at offset 16 and a
    /// 32-bit QKey at offset 20, both in host byte order.
    [[nodiscard]]
    std::array<std::uint8_t, 24> makeEfa(char const* gid, std::uint16_t qpn, std::uint32_t qkey)
    {
        auto buf = std::array<std::uint8_t, 24>{};
        REQUIRE(::inet_pton(AF_INET6, gid, buf.data()) == 1);
        std::memcpy(buf.data() + 16, &qpn, sizeof(qpn));
        std::memcpy(buf.data() + 20, &qkey, sizeof(qkey));
        return buf;
    }
}

TEST_CASE("ofi: FabricAddress default construction", "[ofi][FabricAddress]")
{
    auto const empty = RawFabricAddress{};
    REQUIRE(empty.size() == 0);
    REQUIRE(empty.raw() == nullptr);
    REQUIRE(empty.raw() == static_cast<void*>(nullptr));
    REQUIRE(empty.toBase64().empty());
}

TEST_CASE("ofi: FabricAddress base64 decode", "[ofi][FabricAddress]")
{
    auto const addr = RawFabricAddress::fromBase64("AQIDBAU=", FabricAddressFormat::Unspec); // base64 for {1,2,3,4,5}
    auto* addrInner = static_cast<uint8_t const*>(addr.raw());

    REQUIRE(addr.size() == 5);

    // Simulate a FabricAddress with some data
    std::vector<uint8_t> expected = {1, 2, 3, 4, 5};
    for (size_t i = 0; i < addr.size(); i++)
    {
        REQUIRE(addrInner[i] == expected[i]);
    }

    // Encode again and validate we get the same string
    std::string b64 = addr.toBase64();
    REQUIRE(b64 == "AQIDBAU=");
}

TEST_CASE("ofi: FabricAddress::decode FI_SOCKADDR_IN", "[ofi][FabricAddress]")
{
    auto const native = makeSockaddrIn("10.130.40.13", 9000);
    auto const addr = FabricAddress::decode(FabricAddressFormat::SockaddrIn, &native, sizeof(native));

    REQUIRE_FALSE(addr.empty());
    REQUIRE(std::holds_alternative<::sockaddr_in>(addr.native()));

    auto const& sin = std::get<::sockaddr_in>(addr.native());
    REQUIRE(sin.sin_family == AF_INET);
    REQUIRE(ntohs(sin.sin_port) == 9000);

    char ip[INET_ADDRSTRLEN] = {};
    REQUIRE(::inet_ntop(AF_INET, &sin.sin_addr, ip, sizeof(ip)) != nullptr);
    REQUIRE(std::string{ip} == "10.130.40.13");

    // Renders to the canonical ofi_straddr string.
    REQUIRE(addr.toString() == "fi_sockaddr_in://10.130.40.13:9000");
}

TEST_CASE("ofi: FabricAddress::decode FI_SOCKADDR_IN without a port", "[ofi][FabricAddress]")
{
    auto const native = makeSockaddrIn("10.130.40.13", 0);
    auto const addr = FabricAddress::decode(FabricAddressFormat::SockaddrIn, &native, sizeof(native));

    REQUIRE(std::holds_alternative<::sockaddr_in>(addr.native()));
    auto const& sin = std::get<::sockaddr_in>(addr.native());
    REQUIRE(sin.sin_family == AF_INET);
    REQUIRE(ntohs(sin.sin_port) == 0);

    // An absent port is rendered as :0 by ofi_straddr.
    REQUIRE(addr.toString() == "fi_sockaddr_in://10.130.40.13:0");
}

TEST_CASE("ofi: FabricAddress::decode FI_SOCKADDR_IN6", "[ofi][FabricAddress]")
{
    auto const native = makeSockaddrIn6("2001:db8::1", 443);
    auto const addr = FabricAddress::decode(FabricAddressFormat::SockaddrIn6, &native, sizeof(native));

    REQUIRE(std::holds_alternative<::sockaddr_in6>(addr.native()));
    auto const& sin6 = std::get<::sockaddr_in6>(addr.native());
    REQUIRE(sin6.sin6_family == AF_INET6);
    REQUIRE(ntohs(sin6.sin6_port) == 443);

    REQUIRE(addr.toString() == "fi_sockaddr_in6://[2001:db8::1]:443");
}

TEST_CASE("ofi: FabricAddress::decode FI_ADDR_EFA", "[ofi][FabricAddress]")
{
    auto const native = makeEfa("fe80::1", 12, 34);
    auto const addr = FabricAddress::decode(FabricAddressFormat::Efa, native.data(), native.size());

    REQUIRE(std::holds_alternative<EfaAddress>(addr.native()));
    auto const& efa = std::get<EfaAddress>(addr.native());
    REQUIRE(efa.qpn == 12);
    REQUIRE(efa.qkey == 34);

    REQUIRE(addr.toString() == "fi_addr_efa://[fe80::1]:12:34");
}

TEST_CASE("ofi: FabricAddress::decode FI_SOCKADDR_IB", "[ofi][FabricAddress]")
{
    // P_Key, port space and scope id are hexadecimal; the lower 16 bits of the service id carry
    // the port.
    auto const native = makeSockaddrIb("fe80::1", /*pkey=*/0x1234, /*portSpace=*/0x10b, /*scopeId=*/0xff, /*port=*/0x1f90);
    auto const addr = FabricAddress::decode(FabricAddressFormat::SockaddrIb, &native, sizeof(native));

    REQUIRE(std::holds_alternative<OfiSockaddrIb>(addr.native()));
    auto const& sib = std::get<OfiSockaddrIb>(addr.native());
    REQUIRE(sib.sib_family == AF_IB);
    REQUIRE(ntohs(sib.sib_pkey) == 0x1234);
    REQUIRE(be64toh(sib.sib_scope_id) == 0xff);

    // The service id packs the port space in bits 16..31 and the port in bits 0..15.
    REQUIRE(((be64toh(sib.sib_sid) >> 16) & 0xfff) == 0x10b);
    REQUIRE((be64toh(sib.sib_sid) & 0xffff) == 0x1f90);

    // ofi_straddr does not emit the port, so the rendering drops it (but keeps the port space).
    REQUIRE(addr.toString() == "fi_sockaddr_ib://[fe80::1]:0x1234:0x10b:0xff");
}

TEST_CASE("ofi: FabricAddress::host returns the address portion of each type", "[ofi][FabricAddress]")
{
    auto const sin = makeSockaddrIn("10.130.40.13", 9000);
    REQUIRE(FabricAddress::decode(FabricAddressFormat::SockaddrIn, &sin, sizeof(sin)).node() == "10.130.40.13");

    auto const sin6 = makeSockaddrIn6("2001:db8::1", 443);
    REQUIRE(FabricAddress::decode(FabricAddressFormat::SockaddrIn6, &sin6, sizeof(sin6)).node() == "2001:db8::1");

    auto const efa = makeEfa("fe80::1", 12, 34);
    REQUIRE(FabricAddress::decode(FabricAddressFormat::Efa, efa.data(), efa.size()).node() == "fe80::1");

    auto const sib = makeSockaddrIb("fe80::1", 0x1234, 0x10b, 0xff, 0x1f90);
    REQUIRE(FabricAddress::decode(FabricAddressFormat::SockaddrIb, &sib, sizeof(sib)).node() == "fe80::1");

    // An empty / undecoded address has no host.
    REQUIRE_FALSE(FabricAddress{}.node().has_value());
}

TEST_CASE("ofi: FabricAddress::decode returns an empty address for undecodable input", "[ofi][FabricAddress]")
{
    auto const sin = makeSockaddrIn("10.0.0.1", 9000);

    // A null address buffer.
    REQUIRE(FabricAddress::decode(FabricAddressFormat::SockaddrIn, nullptr, sizeof(sin)).empty());

    // A buffer that is too small for the declared format.
    REQUIRE(FabricAddress::decode(FabricAddressFormat::SockaddrIn, &sin, sizeof(sin) - 1).empty());

    auto const efa = makeEfa("fe80::1", 12, 34);
    REQUIRE(FabricAddress::decode(FabricAddressFormat::SockaddrIn6, efa.data(), efa.size() - 1).empty());

    // An unsupported / unrecognised address format.
    REQUIRE(FabricAddress::decode(FabricAddressFormat::Unspec, &sin, sizeof(sin)).empty());

    // FI_SOCKADDR dispatches on the embedded family; an unknown family does not decode.
    auto unknown = ::sockaddr{};
    unknown.sa_family = AF_UNSPEC;
    REQUIRE(FabricAddress::decode(FabricAddressFormat::Sockaddr, &unknown, sizeof(unknown)).empty());
}
