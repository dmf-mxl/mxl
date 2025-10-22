// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include "Address.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("FabricAddress default construction", "[FabricAddress]")
{
    FabricAddress empty;
    REQUIRE(empty.size() == 0);
    REQUIRE(empty.raw() == nullptr);
    REQUIRE(empty.raw() == static_cast<void*>(nullptr));
    REQUIRE(empty.toBase64().empty());
}

TEST_CASE("FabricAddress base64 decode", "[FabricAddress]")
{
    FabricAddress addr = FabricAddress::fromBase64("AQIDBAU="); // base64 for {1,2,3,4,5}
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
