// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include "RCInitiator.hpp"
#include "RCTarget.hpp"
#include "Util.hpp"

using namespace mxl::lib::fabrics::ofi;

/// Test that we can connect a target and an initiator together.
TEST_CASE("Connection", "[ofi][Connection]")
{
    // setup target
    auto [targetRegions, targetBuffers] = getHostRegionGroups();
    auto targetConf = getDefaultTargetConfig(targetRegions.toAPI());
    auto [rc, targetInfo] = RCTarget::setup(targetConf);

    // setup initiator
    auto [initiatorRegions, initiatorBuffers] = getHostRegionGroups();
    auto initiatorConf = getDefaultInitiatorConfig(initiatorRegions.toAPI());
    auto initiator = RCInitiator::setup(initiatorConf);
    initiator->addTarget(*targetInfo);

    // try to connect them for 5 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        rc->read();
        if (!initiator->makeProgress())
        {
            // connected
            return;
        }
    }
    while (std::chrono::steady_clock::now() < deadline);

    FAIL("Failed to connect in 5 seconds");
}
