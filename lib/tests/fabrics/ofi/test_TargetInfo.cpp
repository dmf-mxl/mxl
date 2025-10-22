// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <picojson/picojson.h>
#include "Address.hpp"
#include "RemoteRegion.hpp"
#include "TargetInfo.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("TargetInfo empty", "[ofi::TargetInfo]")
{
    TargetInfo empty;
    REQUIRE(empty.remoteRegions.empty());
    REQUIRE(empty.fabricAddress.size() == 0);
}

TEST_CASE("TargetInfo deserialize/serialize", "[ofi::TargetInfo]")
{
    auto expectedTagetInfo = TargetInfo{
        .fabricAddress = FabricAddress::fromBase64("bG9jYWxob3N0OjgwODAK"), //   localhost:8080
        .remoteRegions =
            {
                            RemoteRegion{.addr = 1000, .len = 256, .rkey = 0xDEADBEEF},
                            RemoteRegion{.addr = 2000, .len = 256, .rkey = 0xCAFEBABE},
                            },
        .id = 1234,
    };

    // Simply test that we can serialize the object and deserialize back to the original TargetInfo
    auto serialized = expectedTagetInfo.toJSON();
    auto deserialized = TargetInfo::fromJSON(serialized);
    REQUIRE(deserialized == expectedTagetInfo);
}
