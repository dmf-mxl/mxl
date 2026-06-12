// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <mxl/fabrics.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "../Utils.hpp"

namespace
{
    struct InterfaceListStats
    {
        int total = 0;
        int tcp = 0;
        int shm = 0;
    };

    InterfaceListStats countInterfaces(mxlFabricsInterfaceList const* list)
    {
        auto stats = InterfaceListStats{};
        for (auto const* entry = list; entry != nullptr; entry = entry->next)
        {
            ++stats.total;
            if (entry->interface.provider == MXL_FABRICS_PROVIDER_TCP)
            {
                ++stats.tcp;
            }
            else if (entry->interface.provider == MXL_FABRICS_PROVIDER_SHM)
            {
                ++stats.shm;
            }
        }
        return stats;
    }
}

TEST_CASE("Fabrics: GetInterfaces unfiltered returns TCP and SHM", "[fabrics][interfaces]")
{
    auto* instance = mxlCreateInstance("/dev/shm/", "");
    REQUIRE(instance != nullptr);

    mxlFabricsInterfaceList* list = nullptr;
    REQUIRE(mxlFabricsGetInterfaces(instance, nullptr, &list) == MXL_STATUS_OK);
    REQUIRE(list != nullptr);

    auto stats = countInterfaces(list);
    CHECK(stats.total > 0);
    CHECK(stats.tcp > 0);
    CHECK(stats.shm > 0);

    REQUIRE(mxlFabricsFreeInterfaceList(list) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE("Fabrics: GetInterfaces filters by provider", "[fabrics][interfaces]")
{
    auto* instance = mxlCreateInstance("/dev/shm/", "");
    REQUIRE(instance != nullptr);

    SECTION("TCP only")
    {
        auto query = mxlFabricsInterfaceConfig{};
        query.version = MXL_FABRICS_API_VERSION;
        query.provider = MXL_FABRICS_PROVIDER_TCP;

        mxlFabricsInterfaceList* list = nullptr;
        REQUIRE(mxlFabricsGetInterfaces(instance, &query, &list) == MXL_STATUS_OK);
        REQUIRE(list != nullptr);

        for (auto const* entry = list; entry != nullptr; entry = entry->next)
        {
            REQUIRE(entry->interface.provider == MXL_FABRICS_PROVIDER_TCP);
        }

        REQUIRE(mxlFabricsFreeInterfaceList(list) == MXL_STATUS_OK);
    }

    SECTION("SHM only")
    {
        auto query = mxlFabricsInterfaceConfig{};
        query.version = MXL_FABRICS_API_VERSION;
        query.provider = MXL_FABRICS_PROVIDER_SHM;

        mxlFabricsInterfaceList* list = nullptr;
        REQUIRE(mxlFabricsGetInterfaces(instance, &query, &list) == MXL_STATUS_OK);
        REQUIRE(list != nullptr);

        for (auto const* entry = list; entry != nullptr; entry = entry->next)
        {
            REQUIRE(entry->interface.provider == MXL_FABRICS_PROVIDER_SHM);
        }

        REQUIRE(mxlFabricsFreeInterfaceList(list) == MXL_STATUS_OK);
    }

    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE("Fabrics: GetInterfaces ignores capability flags in query", "[fabrics][interfaces]")
{
    auto* instance = mxlCreateInstance("/dev/shm/", "");
    REQUIRE(instance != nullptr);

    mxlFabricsInterfaceList* baseline = nullptr;
    REQUIRE(mxlFabricsGetInterfaces(instance, nullptr, &baseline) == MXL_STATUS_OK);
    auto baselineStats = countInterfaces(baseline);

    auto query = mxlFabricsInterfaceConfig{};
    query.version = MXL_FABRICS_API_VERSION;
    query.caps.version = MXL_FABRICS_API_VERSION;
    query.caps.flags = MXL_FABRICS_IFACE_CAP_REMOTE_WRITE | MXL_FABRICS_IFACE_CAP_SEND_RECEIVE;

    mxlFabricsInterfaceList* filtered = nullptr;
    REQUIRE(mxlFabricsGetInterfaces(instance, &query, &filtered) == MXL_STATUS_OK);
    auto filteredStats = countInterfaces(filtered);

    CHECK(filteredStats.total == baselineStats.total);
    CHECK(filteredStats.tcp == baselineStats.tcp);
    CHECK(filteredStats.shm == baselineStats.shm);

    REQUIRE(mxlFabricsFreeInterfaceList(filtered) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsFreeInterfaceList(baseline) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE("Fabrics: GetInterfaces returns populated capability flags", "[fabrics][interfaces]")
{
    auto* instance = mxlCreateInstance("/dev/shm/", "");
    REQUIRE(instance != nullptr);

    mxlFabricsInterfaceList* list = nullptr;
    REQUIRE(mxlFabricsGetInterfaces(instance, nullptr, &list) == MXL_STATUS_OK);

    for (auto const* entry = list; entry != nullptr; entry = entry->next)
    {
        auto flags = entry->interface.caps.flags;
        CHECK((flags & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS) != 0);
        CHECK((flags & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE) != 0);
    }

    REQUIRE(mxlFabricsFreeInterfaceList(list) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE("Fabrics: GetInterfaces returns non-zero maxMessageSize", "[fabrics][interfaces]")
{
    auto* instance = mxlCreateInstance("/dev/shm/", "");
    REQUIRE(instance != nullptr);

    mxlFabricsInterfaceList* list = nullptr;
    REQUIRE(mxlFabricsGetInterfaces(instance, nullptr, &list) == MXL_STATUS_OK);

    for (auto const* entry = list; entry != nullptr; entry = entry->next)
    {
        CHECK(entry->interface.caps.maxMessageSize > 0);
    }

    REQUIRE(mxlFabricsFreeInterfaceList(list) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Setup with explicit REMOTE_WRITE succeeds", "[fabrics][interfaces][setup]")
{
    auto* instance = mxlCreateInstance(domain.c_str(), "");
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_STATUS_OK);
    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

    auto const caps = mxlFabricsInterfaceCaps{
        .version = MXL_FABRICS_API_VERSION,
        .flags = MXL_FABRICS_IFACE_CAP_REMOTE_WRITE | MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS,
        .maxMessageSize = 0,
    };

    SECTION("TCP")
    {
        mxlFabricsTarget target;
        REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .interface = {.version = MXL_FABRICS_API_VERSION,
                          .provider = MXL_FABRICS_PROVIDER_TCP,
                          .caps = caps,
                          .address = {.node = "127.0.0.1", .service = nullptr},
                          .attr = nullptr},
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        mxlFabricsInitiator initiator;
        REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .interface = {.version = MXL_FABRICS_API_VERSION,
                          .provider = MXL_FABRICS_PROVIDER_TCP,
                          .caps = caps,
                          .address = {.node = "127.0.0.1", .service = nullptr},
                          .attr = nullptr},
            .reader = reader,
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);

        REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    }

    SECTION("SHM")
    {
        mxlFabricsTarget target;
        REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .interface = {.version = MXL_FABRICS_API_VERSION,
                          .provider = MXL_FABRICS_PROVIDER_SHM,
                          .caps = caps,
                          .address = {.node = "target", .service = "caps_test"},
                          .attr = nullptr},
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        mxlFabricsInitiator initiator;
        REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .interface = {.version = MXL_FABRICS_API_VERSION,
                          .provider = MXL_FABRICS_PROVIDER_SHM,
                          .caps = caps,
                          .address = {.node = "initiator", .service = "caps_test"},
                          .attr = nullptr},
            .reader = reader,
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);

        REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    }

    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Setup with BLOCKING_OPERATIONS only defaults to REMOTE_WRITE",
    "[fabrics][interfaces][setup]")
{
    auto* instance = mxlCreateInstance(domain.c_str(), "");
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_STATUS_OK);
    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

    auto const caps = mxlFabricsInterfaceCaps{
        .version = MXL_FABRICS_API_VERSION,
        .flags = MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS,
        .maxMessageSize = 0,
    };

    SECTION("TCP")
    {
        mxlFabricsTarget target;
        REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .interface = {.version = MXL_FABRICS_API_VERSION,
                          .provider = MXL_FABRICS_PROVIDER_TCP,
                          .caps = caps,
                          .address = {.node = "127.0.0.1", .service = nullptr},
                          .attr = nullptr},
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    }

    SECTION("SHM")
    {
        mxlFabricsTarget target;
        REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .interface = {.version = MXL_FABRICS_API_VERSION,
                          .provider = MXL_FABRICS_PROVIDER_SHM,
                          .caps = caps,
                          .address = {.node = "target", .service = "blocking_test"},
                          .attr = nullptr},
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    }

    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Setup with empty caps defaults to REMOTE_WRITE", "[fabrics][interfaces][setup]")
{
    auto* instance = mxlCreateInstance(domain.c_str(), "");
    REQUIRE(instance != nullptr);

    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_STATUS_OK);
    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

    SECTION("TCP")
    {
        mxlFabricsTarget target;
        REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .interface = {.version = MXL_FABRICS_API_VERSION,
                          .provider = MXL_FABRICS_PROVIDER_TCP,
                          .caps = {},
                          .address = {.node = "127.0.0.1", .service = nullptr},
                          .attr = nullptr},
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    }

    SECTION("SHM")
    {
        mxlFabricsTarget target;
        REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .interface = {.version = MXL_FABRICS_API_VERSION,
                          .provider = MXL_FABRICS_PROVIDER_SHM,
                          .caps = {},
                          .address = {.node = "target", .service = "empty_test"},
                          .attr = nullptr},
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    }

    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}
