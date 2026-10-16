// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <catch2/catch_test_macros.hpp>
#include "Address.hpp"
#include "FabricAddress.hpp"
#include "NotifyMode.hpp"
#include "Provider.hpp"
#include "RemoteRegion.hpp"
#include "TargetInfo.hpp"

using namespace mxl::lib::fabrics::ofi;

namespace
{
    /// sockaddr_in for 127.0.0.1:9090, base64 encoded.
    constexpr auto addressBase64 = "AgAjgn8AAAEAAAAAAAAAAA==";

    /** \brief Build a TargetInfo with fixed id, address, provider and one remote region.
     * \param[in] notifyMode The notify mode to store in the TargetInfo.
     * \return The TargetInfo.
     */
    [[nodiscard]]
    TargetInfo makeTargetInfo(NotifyMode notifyMode)
    {
        return {.id = 42,
            .fabricAddress = RawFabricAddress::fromBase64(addressBase64, FabricAddressFormat::SockaddrIn),
            .provider = Provider::VERBS,
            .remoteRegions = {RemoteRegion{0x1000, 4096, 7}},
            .bounceBufferInfo = std::nullopt,
            .notifyMode = notifyMode};
    }

    /** \brief TargetInfo JSON in the format written before the notify field existed.
     * \return JSON describing the same target as makeTargetInfo(NotifyMode::Immediate).
     */
    [[nodiscard]]
    std::string legacyJson()
    {
        return std::string{R"({"addressFormat":)"} + std::to_string(FI_SOCKADDR_IN) + R"(,"fabricAddress":")" + addressBase64 +
               R"(","id":"42","provider":"verbs","regions":[{"addr":"4096","len":"4096","rkey":"7"}]})";
    }
}

TEST_CASE("ofi: TargetInfo - Immediate mode is not serialized", "[ofi][TargetInfo][NotifyMode]")
{
    auto const info = makeTargetInfo(NotifyMode::Immediate);
    auto const json = info.toJSON();

    REQUIRE(json.find("notify") == std::string::npos);

    auto const parsed = TargetInfo::fromJSON(json);
    REQUIRE(parsed.notifyMode == NotifyMode::Immediate);
    REQUIRE(parsed == info);
}

TEST_CASE("ofi: TargetInfo - Send mode round trip", "[ofi][TargetInfo][NotifyMode]")
{
    auto const info = makeTargetInfo(NotifyMode::Send);
    auto const json = info.toJSON();

    REQUIRE(json.find(R"("notify":"send")") != std::string::npos);

    auto const parsed = TargetInfo::fromJSON(json);
    REQUIRE(parsed.notifyMode == NotifyMode::Send);
    REQUIRE(parsed == info);
    REQUIRE_FALSE(parsed == makeTargetInfo(NotifyMode::Immediate));
}

TEST_CASE("ofi: TargetInfo - JSON without a notify field parses as immediate", "[ofi][TargetInfo][NotifyMode]")
{
    auto const parsed = TargetInfo::fromJSON(legacyJson());

    REQUIRE(parsed.notifyMode == NotifyMode::Immediate);
    REQUIRE(parsed == makeTargetInfo(NotifyMode::Immediate));
}

TEST_CASE("ofi: TargetInfo - Unknown notify value is rejected", "[ofi][TargetInfo][NotifyMode]")
{
    auto json = legacyJson();
    json.pop_back(); // Remove the closing brace so a field can be appended.

    REQUIRE_THROWS(TargetInfo::fromJSON(json + R"(,"notify":"bogus"})"));
}
