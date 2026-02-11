// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_Provider.cpp
 * @brief Unit tests for MXL libfabric provider enumeration and conversion
 *
 * This test suite validates provider type conversions between:
 *   - Internal enum (Provider::TCP, Provider::VERBS, etc.)
 *   - Public API enum (mxlFabricsProvider)
 *   - String representations ("tcp", "verbs", "efa", "shm")
 *
 * Supported providers:
 *   - TCP: Ethernet-based reliable transport (widely compatible)
 *   - VERBS: InfiniBand/RoCE using libibverbs
 *   - EFA: AWS Elastic Fabric Adapter
 *   - SHM: Shared memory for same-host communication
 *
 * These tests ensure consistent provider naming across MXL's
 * internal implementation and public API.
 */

#include <catch2/catch_test_macros.hpp>
#include "Provider.hpp"

using namespace mxl::lib::fabrics::ofi;

/**
 * @brief Test internal provider enum to public API enum conversion
 *
 * Verifies that providerToAPI correctly maps internal Provider enum
 * values to their corresponding mxlFabricsProvider API values.
 */
TEST_CASE("ofi: Provider enum to API conversion", "[ofi][Provider]")
{
    REQUIRE(providerToAPI(Provider::TCP) == MXL_SHARING_PROVIDER_TCP);
    REQUIRE(providerToAPI(Provider::VERBS) == MXL_SHARING_PROVIDER_VERBS);
    REQUIRE(providerToAPI(Provider::EFA) == MXL_SHARING_PROVIDER_EFA);
    REQUIRE(providerToAPI(Provider::SHM) == MXL_SHARING_PROVIDER_SHM);
}

/**
 * @brief Test public API enum to internal provider enum conversion
 *
 * Verifies that providerFromAPI correctly maps public mxlFabricsProvider
 * values to their internal Provider enum equivalents. Also tests that
 * invalid provider values return std::nullopt.
 */
TEST_CASE("ofi: Provider enum from API conversion", "[ofi][Provider]")
{
    REQUIRE(providerFromAPI(MXL_SHARING_PROVIDER_TCP) == Provider::TCP);
    REQUIRE(providerFromAPI(MXL_SHARING_PROVIDER_VERBS) == Provider::VERBS);
    REQUIRE(providerFromAPI(MXL_SHARING_PROVIDER_EFA) == Provider::EFA);
    REQUIRE(providerFromAPI(MXL_SHARING_PROVIDER_SHM) == Provider::SHM);
    REQUIRE_FALSE(providerFromAPI(static_cast<mxlFabricsProvider>(999)).has_value());
}

/**
 * @brief Test string to provider enum conversion
 *
 * Verifies that providerFromString correctly parses provider name strings
 * (used in command-line arguments and configuration files). Tests both
 * valid provider names and invalid strings that should return std::nullopt.
 */
TEST_CASE("ofi: Provider from string", "[ofi][Provider]")
{
    REQUIRE(providerFromString("tcp") == Provider::TCP);
    REQUIRE(providerFromString("verbs") == Provider::VERBS);
    REQUIRE(providerFromString("efa") == Provider::EFA);
    REQUIRE(providerFromString("shm") == Provider::SHM);

    REQUIRE_FALSE(providerFromString("invalid").has_value());
    REQUIRE_FALSE(providerFromString("foo").has_value());
}
