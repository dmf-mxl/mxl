// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include "Provider.hpp"

using namespace mxl::lib::fabrics::ofi;

TEST_CASE("ofi: Provider enum to API conversion", "[ofi][Provider]")
{
    REQUIRE(providerToAPI(Provider::TCP) == MXL_SHARING_PROVIDER_TCP);
    REQUIRE(providerToAPI(Provider::VERBS) == MXL_SHARING_PROVIDER_VERBS);
    REQUIRE(providerToAPI(Provider::EFA) == MXL_SHARING_PROVIDER_EFA);
    REQUIRE(providerToAPI(Provider::SHM) == MXL_SHARING_PROVIDER_SHM);
}

TEST_CASE("ofi: Provider enum from API conversion", "[ofi][Provider]")
{
    REQUIRE(providerFromAPI(MXL_SHARING_PROVIDER_TCP) == Provider::TCP);
    REQUIRE(providerFromAPI(MXL_SHARING_PROVIDER_VERBS) == Provider::VERBS);
    REQUIRE(providerFromAPI(MXL_SHARING_PROVIDER_EFA) == Provider::EFA);
    REQUIRE(providerFromAPI(MXL_SHARING_PROVIDER_SHM) == Provider::SHM);
    REQUIRE_FALSE(providerFromAPI(static_cast<mxlFabricsProvider>(999)).has_value());
}

TEST_CASE("ofi: Provider from string", "[ofi][Provider]")
{
    REQUIRE(providerFromString("tcp") == Provider::TCP);
    REQUIRE(providerFromString("verbs") == Provider::VERBS);
    REQUIRE(providerFromString("efa") == Provider::EFA);
    REQUIRE(providerFromString("shm") == Provider::SHM);

    REQUIRE_FALSE(providerFromString("invalid").has_value());
    REQUIRE_FALSE(providerFromString("foo").has_value());
}
