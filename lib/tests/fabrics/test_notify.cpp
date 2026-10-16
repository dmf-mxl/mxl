// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mxl/fabrics.h>
#include "mxl/flow.h"
#include "mxl/mxl.h"
#include "../Utils.hpp"
#include "NotifyEnvGuard.hpp"

#ifdef MXL_FABRICS_OFI
// clang-format off
    #include "TargetInfo.hpp"
// clang-format on
#endif

namespace
{
    constexpr auto POLL_TIMEOUT = std::chrono::seconds(5);

    /** \brief Interface settings for a tcp endpoint on the loopback address.
     * \return The interface configuration.
     */
    [[nodiscard]]
    mxlFabricsInterfaceConfig tcpLoopback()
    {
        return {
            .version = MXL_FABRICS_API_VERSION,
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .caps =
                {
                       .version = MXL_FABRICS_API_VERSION,
                       .flags = MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS | MXL_FABRICS_IFACE_CAP_REMOTE_WRITE,
                       .maxMessageSize = 0,
                       },
            .address = {.node = "127.0.0.1",                                  .service = nullptr                                                                                                              },
            .attr = nullptr
        };
    }

    /** \brief Drive target and initiator until the initiator reports that it has no pending work, which includes the connection.
     * \param[in] target The target.
     * \param[in] initiator The initiator.
     */
    void connect(mxlFabricsTarget target, mxlFabricsInitiator initiator)
    {
        auto const deadline = std::chrono::steady_clock::now() + POLL_TIMEOUT;
        auto grainIndex = std::uint64_t{};
        while (std::chrono::steady_clock::now() < deadline)
        {
            (void)mxlFabricsTargetReadGrainNonBlocking(target, &grainIndex);
            auto const status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
            if (status == MXL_STATUS_OK)
            {
                return;
            }
            REQUIRE(status == MXL_ERR_NOT_READY);
        }
        FAIL("Failed to connect in 5 seconds");
    }

    /** \brief Read one transfer report from the target, if there is one.
     * \param[in] target The target.
     * \return 1 if the target reported a transfer, 0 otherwise.
     */
    [[nodiscard]]
    std::size_t pollTarget(mxlFabricsTarget target)
    {
        auto grainIndex = std::uint64_t{};
        auto const status = mxlFabricsTargetReadGrainNonBlocking(target, &grainIndex);
        if (status == MXL_STATUS_OK)
        {
            return 1;
        }
        REQUIRE(status == MXL_ERR_NOT_READY);
        return 0;
    }

    /** \brief Drive both sides until the initiator reports that it has no pending work. The target is polled too, because some providers
     * (tcp) only complete transfers while the target makes progress.
     * \param[in] target The target.
     * \param[in] initiator The initiator.
     * \return The number of transfers the target reported in the meantime.
     */
    [[nodiscard]]
    std::size_t waitForInitiatorDone(mxlFabricsTarget target, mxlFabricsInitiator initiator)
    {
        auto const deadline = std::chrono::steady_clock::now() + POLL_TIMEOUT;
        auto reported = std::size_t{0};
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto const status = mxlFabricsInitiatorMakeProgressNonBlocking(initiator);
            if (status == MXL_STATUS_OK)
            {
                return reported;
            }
            REQUIRE(status == MXL_ERR_NOT_READY);
            reported += pollTarget(target);
        }
        FAIL("Initiator still had pending work after 5 seconds");
        return reported;
    }

    /** \brief Count the transfers the target reports.
     * \param[in] target The target.
     * \param[in] expected Stop after this many transfers.
     * \return The number of transfers the target reported before the timeout.
     */
    [[nodiscard]]
    std::size_t countReportedTransfers(mxlFabricsTarget target, std::size_t expected)
    {
        auto const deadline = std::chrono::steady_clock::now() + POLL_TIMEOUT;
        auto reported = std::size_t{0};
        while ((reported < expected) && (std::chrono::steady_clock::now() < deadline))
        {
            reported += pollTarget(target);
        }
        return reported;
    }
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Notify modes between mixed peers", "[fabrics][notify]")
{
    // Each section sets MXL_FABRICS_NOTIFY separately for the target and the initiator, so the two sides of one process act like peers built
    // or configured differently. Every slice transfer must be reported by the target exactly once, whichever way it was notified.
    auto targetEnv = static_cast<char const*>(nullptr);
    auto initiatorEnv = static_cast<char const*>(nullptr);
    auto targetAdvertisesSend = false;

    SECTION("Both sides use immediate data")
    {}

    SECTION("Target asks for send notifications")
    {
        targetEnv = "send";
        targetAdvertisesSend = true;
    }

    SECTION("Initiator sends notifications to a target that advertises immediate data")
    {
        initiatorEnv = "send";
    }

    SECTION("Both sides use send notifications")
    {
        targetEnv = "send";
        initiatorEnv = "send";
        targetAdvertisesSend = true;
    }

    auto instance = mxlCreateInstance(domain.c_str(), "");

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFabricsTargetInfo targetInfo;
    {
        auto const env = mxl::tests::NotifyEnvGuard{targetEnv};
        auto const targetConfig = mxlFabricsTargetConfig{.version = MXL_FABRICS_API_VERSION, .interface = tcpLoopback(), .writer = writer};
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);
    }

#ifdef MXL_FABRICS_OFI
    namespace ofi = mxl::lib::fabrics::ofi;
    auto const expectedMode = targetAdvertisesSend ? ofi::NotifyMode::Send : ofi::NotifyMode::Immediate;
    REQUIRE(ofi::TargetInfo::fromAPI(targetInfo)->notifyMode == expectedMode);
#endif

    {
        auto const env = mxl::tests::NotifyEnvGuard{initiatorEnv};
        auto const initiatorConfig = mxlFabricsInitiatorConfig{.version = MXL_FABRICS_API_VERSION, .interface = tcpLoopback(), .reader = reader};
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);
    }

    connect(target, initiator);

    // Post a burst of single slice transfers without letting the target read in between. Each one produces one notification.
    constexpr auto sliceCount = std::uint16_t{32};
    for (auto slice = std::uint16_t{0}; slice < sliceCount; ++slice)
    {
        REQUIRE(mxlFabricsInitiatorTransferGrain(initiator, 0, slice, slice + 1) == MXL_STATUS_OK);
    }

    // Once the initiator reports no pending work, every notification has been delivered. Destroying the initiator right away must not lose
    // any of them, so the target reports all transfers on its own afterwards.
    auto const reportedBeforeTeardown = waitForInitiatorDone(target, initiator);
    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);

    REQUIRE(reportedBeforeTeardown + countReportedTransfers(target, sliceCount - reportedBeforeTeardown) == sliceCount);

    // No transfer is reported twice.
    auto grainIndex = std::uint64_t{};
    REQUIRE(mxlFabricsTargetReadGrainNonBlocking(target, &grainIndex) == MXL_ERR_NOT_READY);

    mxlFabricsFreeTargetInfo(targetInfo);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}
