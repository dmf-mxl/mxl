// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
#include <uuid.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <picojson/wrapper.h>
#include <rdma/fabric.h>
#include <mxl/fabrics.h>
#include "mxl/flow.h"
#include "mxl/mxl.h"
#include "../Utils.hpp"

#ifdef MXL_FABRICS_OFI
// clang-format off
    #include "TargetInfo.hpp"
// clang-format on
#else
#endif

namespace
{
    constexpr auto POLL_TIMEOUT = std::chrono::seconds(5);
    constexpr auto BLOCKING_WAIT = std::chrono::milliseconds(20);

    template<typename ProgressFn>
    void requireProgressStatus(ProgressFn&& progressFn, char const* actor)
    {
        auto status = progressFn();
        if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
        {
            FAIL(std::string{"Something went wrong in the "} + actor + ": " + std::to_string(status));
        }
    }

    template<typename StepFn>
    void pollUntilSuccess(StepFn&& stepFn, std::chrono::steady_clock::duration timeout, char const* timeoutMessage)
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        do
        {
            if (stepFn())
            {
                return;
            }
        }
        while (std::chrono::steady_clock::now() < deadline);

        FAIL(timeoutMessage);
    }

    template<typename TargetProgressFn, typename InitiatorProgressFn>
    void waitForConnection(TargetProgressFn&& targetProgressFn, InitiatorProgressFn&& initiatorProgressFn)
    {
        pollUntilSuccess(
            [&]()
            {
                targetProgressFn();

                auto status = initiatorProgressFn();
                if (status != MXL_STATUS_OK && status != MXL_ERR_NOT_READY)
                {
                    FAIL("Something went wrong in the initiator: " + std::to_string(status));
                }

                return status == MXL_STATUS_OK;
            },
            POLL_TIMEOUT,
            "Failed to connect in 5 seconds");
    }

    template<typename Targets, typename TargetProgressFn, typename InitiatorProgressFn>
    void waitForAllConnections(Targets const& targets, TargetProgressFn&& targetProgressFn, InitiatorProgressFn&& initiatorProgressFn)
    {
        waitForConnection(
            [&]()
            {
                for (auto& target : targets)
                {
                    targetProgressFn(target);
                }
            },
            std::forward<InitiatorProgressFn>(initiatorProgressFn));
    }

    template<typename TargetProgressFn, typename InitiatorProgressFn, typename TransferFn>
    void waitForTransferStart(TargetProgressFn&& targetProgressFn, InitiatorProgressFn&& initiatorProgressFn, TransferFn&& transferFn)
    {
        pollUntilSuccess(
            [&]()
            {
                targetProgressFn();
                initiatorProgressFn();
                return transferFn() == MXL_STATUS_OK;
            },
            POLL_TIMEOUT,
            "Failed to start transfer in 5 seconds");
    }

    template<typename Targets, typename TargetProgressFn, typename InitiatorProgressFn, typename TransferFn>
    void waitForAllTransfersStart(Targets const& targets, TargetProgressFn&& targetProgressFn, InitiatorProgressFn&& initiatorProgressFn,
        TransferFn&& transferFn)
    {
        waitForTransferStart(
            [&]()
            {
                for (auto& target : targets)
                {
                    targetProgressFn(target);
                }
            },
            std::forward<InitiatorProgressFn>(initiatorProgressFn),
            std::forward<TransferFn>(transferFn));
    }

    template<typename InitiatorProgressFn, typename CompletionFn>
    void waitForTransferCompletion(InitiatorProgressFn&& initiatorProgressFn, CompletionFn&& completionFn)
    {
        pollUntilSuccess(
            [&]()
            {
                initiatorProgressFn();

                auto status = completionFn();
                if (status == MXL_ERR_INTERRUPTED)
                {
                    FAIL("Peer disconnected before the transfer completed");
                }

                return status == MXL_STATUS_OK;
            },
            POLL_TIMEOUT,
            "Transfer did not complete in 5 seconds");
    }

    template<typename Targets, typename InitiatorProgressFn, typename CompletionFn>
    void waitForAllTransfersCompletion(Targets const& targets, InitiatorProgressFn&& initiatorProgressFn, CompletionFn&& completionFn)
    {
        auto transfer_complete = std::vector<bool>(targets.size(), false);

        pollUntilSuccess(
            [&]()
            {
                initiatorProgressFn();

                for (size_t i = 0; i < targets.size(); i++)
                {
                    auto status = completionFn(targets[i]);
                    if (status == MXL_ERR_INTERRUPTED)
                    {
                        FAIL("Peer disconnected before the transfer completed");
                    }
                    if (status == MXL_STATUS_OK)
                    {
                        transfer_complete[i] = true;
                    }
                }

                return std::ranges::all_of(transfer_complete, [](bool v) { return v; });
            },
            POLL_TIMEOUT,
            "Transfer did not complete in 5 seconds");
    }
}

TEST_CASE("Fabrics basic creation/destroy", "[fabrics][basics]")
{
    auto instance = mxlCreateInstance("/dev/shm/", "");

    mxlFabricsInstance fabrics;
    SECTION("instance creation/destruction")
    {
        REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

        SECTION("target creation/destruction")
        {
            mxlFabricsTarget target;
            REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);
            REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
        }

        SECTION("initiator creation/destruction")
        {
            mxlFabricsInitiator initiator;
            REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);
            REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
        }

        REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    }
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics connection oriented activation tests", "[fabrics][connected][activation]")
{
    auto instance = mxlCreateInstance(domain.c_str(), "");

    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    // A flow writer and reader provide the memory regions for the target and initiator respectively.
    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    SECTION("target/initiator setup")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .reader = reader,
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);

        SECTION("initiator add/remove target")
        {
            REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

            SECTION("non-blocking")
            {
                std::uint64_t dummyIndex;
                waitForConnection([&]() { mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); },
                    [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });
            }

            SECTION("blocking")
            {
                std::uint64_t dummyIndex;
                waitForConnection([&]() { mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); },
                    [&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); });
            }

            REQUIRE(mxlFabricsInitiatorRemoveTarget(initiator, targetInfo) == MXL_STATUS_OK);
        }

        REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
    }

    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics connectionless activation tests", "[fabrics][connectionless][activation]")
{
    auto instance = mxlCreateInstance(domain.c_str(), "");

    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    auto const flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    // A flow writer and reader provide the memory regions for the target and initiator respectively.
    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_STATUS_OK);

    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    auto targetConfig = mxlFabricsTargetConfig{
        .version = MXL_FABRICS_API_VERSION,
        .endpointAddress = mxlFabricsEndpointAddress{.node = "target", .service = "activation"},
        .provider = MXL_FABRICS_PROVIDER_SHM,
        .writer = writer,
    };
    mxlFabricsTargetInfo targetInfo;
    REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

    auto initiatorConfig = mxlFabricsInitiatorConfig{
        .version = MXL_FABRICS_API_VERSION,
        .endpointAddress = mxlFabricsEndpointAddress{.node = "initiator", .service = "activation"},
        .provider = MXL_FABRICS_PROVIDER_SHM,
        .reader = reader,
    };
    REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);

    SECTION("initiator add/remove target")
    {
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorRemoveTarget(initiator, targetInfo) == MXL_STATUS_OK);
    }
    REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

    SECTION("non-blocking")
    {
        std::uint64_t dummyIndex;
        waitForConnection([&]() { mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); },
            [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });
    }

    SECTION("blocking")
    {
        std::uint64_t dummyIndex;
        waitForConnection([&]() { mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); },
            [&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); });
    }

    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsFreeTargetInfo(targetInfo) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Transfer Grain with flows", "[Fabrics][Transfer][Flows][Grain]")
{
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

    // Initiator
    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    SECTION("RC")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .reader = reader,
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

        std::uint64_t dummyIndex;
        waitForConnection([&]() { mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); },
            [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });

        SECTION("non-blocking")
        {
            waitForTransferStart([&]() { mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); },
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1); });

            waitForTransferCompletion([&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); });
        }

        SECTION("blocking")
        {
            waitForTransferStart([&]() { mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); },
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1); });

            waitForTransferCompletion([&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); });
        }

        mxlFabricsFreeTargetInfo(targetInfo);
    }

    SECTION("RDM")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "target", .service = "test"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "initiator", .service = "test"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .reader = reader,
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

        std::uint64_t dummyIndex;
        waitForConnection([&]() { mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); },
            [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });

        SECTION("non-blocking")
        {
            waitForTransferStart([&]() { mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); },
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1); });

            waitForTransferCompletion([&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); });
        }

        SECTION("blocking")
        {
            waitForTransferStart([&]() { mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); },
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1); });

            waitForTransferCompletion([&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); });
        }

        mxlFabricsFreeTargetInfo(targetInfo);
    }

    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Transfer Sample with flows", "[Fabrics][Transfer][Flows][Sample]")
{
    auto instance = mxlCreateInstance(domain.c_str(), "");

    mxlFabricsInstance fabrics;
    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

    mxlFabricsTarget target;
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    auto flowDef = mxl::tests::readFile("../data/audio_flow.json");
    auto const flowId = "b3bb5be7-9fe9-4324-a5bb-4c70e1084449";

    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_STATUS_OK);

    // Initiator
    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, flowId, "", &reader) == MXL_STATUS_OK);

    SECTION("RC")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .reader = reader,
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

        std::uint64_t dummyIndex;
        std::size_t dummyCount;
        waitForConnection([&]() { mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); },
            [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });

        SECTION("non-blocking")
        {
            waitForTransferStart([&]() { mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); },
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferSamples(initiator, 0, 480); });

            waitForTransferCompletion([&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); });
        }

        SECTION("blocking")
        {
            waitForTransferStart([&]() { mxlFabricsTargetReadSamples(target, BLOCKING_WAIT.count(), &dummyIndex, &dummyCount); },
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferSamples(initiator, 0, 480); });

            waitForTransferCompletion([&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsTargetReadSamples(target, BLOCKING_WAIT.count(), &dummyIndex, &dummyCount); });
        }

        mxlFabricsFreeTargetInfo(targetInfo);
    }

    SECTION("RDM")
    {
        auto targetConfig = mxlFabricsTargetConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "target", .service = "test"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .writer = writer,
        };
        mxlFabricsTargetInfo targetInfo;
        REQUIRE(mxlFabricsTargetSetup(target, &targetConfig, nullptr, &targetInfo) == MXL_STATUS_OK);

        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "initiator", .service = "test"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .reader = reader,
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo) == MXL_STATUS_OK);

        std::uint64_t dummyIndex;
        std::size_t dummyCount;
        waitForConnection([&]() { mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); },
            [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });

        SECTION("non-blocking")
        {
            waitForTransferStart([&]() { mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); },
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferSamples(initiator, 0, 480); });

            waitForTransferCompletion([&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); });
        }

        SECTION("blocking")
        {
            waitForTransferStart([&]() { mxlFabricsTargetReadSamples(target, BLOCKING_WAIT.count(), &dummyIndex, &dummyCount); },
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferSamples(initiator, 0, 480); });

            waitForTransferCompletion([&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsTargetReadSamples(target, BLOCKING_WAIT.count(), &dummyIndex, &dummyCount); });
        }

        mxlFabricsFreeTargetInfo(targetInfo);
    }

    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Transfer Grain with flows multi target",
    "[Fabrics][Transfer][Flows][Grain][Multi-targets]")
{
    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    auto jsonValue = picojson::value{};
    REQUIRE(picojson::parse(jsonValue, flowDef).empty());
    REQUIRE(jsonValue.is<picojson::object>());
    auto root = jsonValue.get<picojson::object>();

    mxlFabricsInstance fabrics;
    auto instance = mxlCreateInstance(domain.c_str(), "");
    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

    constexpr auto nbTargets = 2;
    std::array<mxlFabricsTarget, nbTargets> targets;
    std::array<std::string, nbTargets> flowIds;
    std::array<std::string, nbTargets> flowDefs;
    std::array<mxlFlowConfigInfo, nbTargets> configInfo;
    std::array<mxlFlowWriter, nbTargets> writer;
    for (size_t i = 0; i < nbTargets; i++)
    {
        REQUIRE(mxlFabricsCreateTarget(fabrics, &targets[i]) == MXL_STATUS_OK);
        flowIds[i] = uuids::to_string(uuids::uuid_system_generator{}());
        root.at("id") = picojson::value{flowIds[i]};
        flowDefs[i] = picojson::value{root}.serialize();
        REQUIRE(mxlCreateFlowWriter(instance, flowDefs[i].c_str(), nullptr, &writer[i], &configInfo[i], nullptr) == MXL_STATUS_OK);
    }

    // Initiator
    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, root.at("id").get<std::string>().c_str(), "", &reader) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    SECTION("RC")
    {
        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .reader = reader,
        };

        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);

        std::array<mxlFabricsTargetConfig, nbTargets> targetConfig;
        std::array<mxlFabricsTargetInfo, nbTargets> targetInfo;
        for (size_t i = 0; i < nbTargets; i++)
        {
            targetConfig[i] = mxlFabricsTargetConfig{
                .version = MXL_FABRICS_API_VERSION,
                .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
                .provider = MXL_FABRICS_PROVIDER_TCP,
                .writer = writer[i],
            };
            REQUIRE(mxlFabricsTargetSetup(targets[i], &targetConfig[i], nullptr, &targetInfo[i]) == MXL_STATUS_OK);

            REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo[i]) == MXL_STATUS_OK);
        }

        std::uint64_t dummyIndex;
        waitForAllConnections(
            targets,
            [&](auto& target) { requireProgressStatus([&]() { return mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); }, "target"); },
            [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });

        SECTION("non-blocking")
        {
            waitForAllTransfersStart(
                targets,
                [&](auto& target) { mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); },
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1); });

            waitForAllTransfersCompletion(
                targets,
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&](auto const& target) { return mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); });
        }

        SECTION("blocking")
        {
            waitForAllTransfersStart(
                targets,
                [&](auto& target) { mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); },
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1); });

            waitForAllTransfersCompletion(
                targets,
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&](auto const& target) { return mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); });
        }

        for (size_t i = 0; i < nbTargets; i++)
        {
            mxlFabricsFreeTargetInfo(targetInfo[i]);
        }
    }

    SECTION("RDM")
    {
        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "initiator", .service = "test"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .reader = reader,
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);

        std::array<mxlFabricsTargetConfig, nbTargets> targetConfig;
        std::array<mxlFabricsTargetInfo, nbTargets> targetInfo;
        for (size_t i = 0; i < nbTargets; i++)
        {
            targetConfig[i] = mxlFabricsTargetConfig{
                .version = MXL_FABRICS_API_VERSION,
                .endpointAddress = mxlFabricsEndpointAddress{.node = "target", .service = "test"},
                .provider = MXL_FABRICS_PROVIDER_SHM,
                .writer = writer[i],
            };
            REQUIRE(mxlFabricsTargetSetup(targets[i], &targetConfig[i], nullptr, &targetInfo[i]) == MXL_STATUS_OK);

            REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo[i]) == MXL_STATUS_OK);
        }

        std::uint64_t dummyIndex;
        waitForAllConnections(
            targets,
            [&](auto& target) { requireProgressStatus([&]() { return mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); }, "target"); },
            [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });

        SECTION("non-blocking")
        {
            waitForAllTransfersStart(
                targets,
                [&](auto& target) { mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); },
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1); });

            waitForAllTransfersCompletion(
                targets,
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&](auto const& target) { return mxlFabricsTargetReadGrainNonBlocking(target, &dummyIndex); });
        }

        SECTION("blocking")
        {
            waitForAllTransfersStart(
                targets,
                [&](auto& target) { mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); },
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferGrain(initiator, 0, 0, 1); });

            waitForAllTransfersCompletion(
                targets,
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&](auto const& target) { return mxlFabricsTargetReadGrain(target, BLOCKING_WAIT.count(), &dummyIndex); });
        }

        for (size_t i = 0; i < nbTargets; i++)
        {
            mxlFabricsFreeTargetInfo(targetInfo[i]);
        }
    }

    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    for (size_t i = 0; i < nbTargets; i++)
    {
        REQUIRE(mxlReleaseFlowWriter(instance, writer[i]) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsDestroyTarget(fabrics, targets[i]) == MXL_STATUS_OK);
    }
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: Transfer Samples with flows multi target",
    "[Fabrics][Transfer][Flows][Samples][Multi-targets]")
{
    auto flowDef = mxl::tests::readFile("../data/audio_flow.json");
    auto jsonValue = picojson::value{};
    REQUIRE(picojson::parse(jsonValue, flowDef).empty());
    REQUIRE(jsonValue.is<picojson::object>());
    auto root = jsonValue.get<picojson::object>();

    mxlFabricsInstance fabrics;
    auto instance = mxlCreateInstance(domain.c_str(), "");
    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);

    constexpr auto nbTargets = 2;
    std::array<mxlFabricsTarget, nbTargets> targets;
    std::array<std::string, nbTargets> flowIds;
    std::array<std::string, nbTargets> flowDefs;
    std::array<mxlFlowConfigInfo, nbTargets> configInfo;
    std::array<mxlFlowWriter, nbTargets> writer;
    for (size_t i = 0; i < nbTargets; i++)
    {
        REQUIRE(mxlFabricsCreateTarget(fabrics, &targets[i]) == MXL_STATUS_OK);
        flowIds[i] = uuids::to_string(uuids::uuid_system_generator{}());
        root.at("id") = picojson::value{flowIds[i]};
        flowDefs[i] = picojson::value{root}.serialize();
        REQUIRE(mxlCreateFlowWriter(instance, flowDefs[i].c_str(), nullptr, &writer[i], &configInfo[i], nullptr) == MXL_STATUS_OK);
    }

    // Initiator
    mxlFlowReader reader;
    REQUIRE(mxlCreateFlowReader(instance, root.at("id").get<std::string>().c_str(), "", &reader) == MXL_STATUS_OK);

    mxlFabricsInitiator initiator;
    REQUIRE(mxlFabricsCreateInitiator(fabrics, &initiator) == MXL_STATUS_OK);

    SECTION("RC")
    {
        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
            .provider = MXL_FABRICS_PROVIDER_TCP,
            .reader = reader,
        };

        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);

        std::array<mxlFabricsTargetConfig, nbTargets> targetConfig;
        std::array<mxlFabricsTargetInfo, nbTargets> targetInfo;
        for (size_t i = 0; i < nbTargets; i++)
        {
            targetConfig[i] = mxlFabricsTargetConfig{
                .version = MXL_FABRICS_API_VERSION,
                .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
                .provider = MXL_FABRICS_PROVIDER_TCP,
                .writer = writer[i],
            };
            REQUIRE(mxlFabricsTargetSetup(targets[i], &targetConfig[i], nullptr, &targetInfo[i]) == MXL_STATUS_OK);

            REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo[i]) == MXL_STATUS_OK);
        }

        std::uint64_t dummyIndex;
        std::size_t dummyCount;
        waitForAllConnections(
            targets,
            [&](auto& target)
            { requireProgressStatus([&]() { return mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); }, "target"); },
            [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });

        SECTION("non-blocking")
        {
            waitForAllTransfersStart(
                targets,
                [&](auto& target) { mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); },
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferSamples(initiator, 0, 480); });

            waitForAllTransfersCompletion(
                targets,
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&](auto const& target) { return mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); });
        }

        SECTION("blocking")
        {
            waitForAllTransfersStart(
                targets,
                [&](auto& target) { mxlFabricsTargetReadSamples(target, BLOCKING_WAIT.count(), &dummyIndex, &dummyCount); },
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferSamples(initiator, 0, 480); });

            waitForAllTransfersCompletion(
                targets,
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&](auto const& target) { return mxlFabricsTargetReadSamples(target, BLOCKING_WAIT.count(), &dummyIndex, &dummyCount); });
        }

        for (size_t i = 0; i < nbTargets; i++)
        {
            mxlFabricsFreeTargetInfo(targetInfo[i]);
        }
    }

    SECTION("RDM")
    {
        auto initiatorConfig = mxlFabricsInitiatorConfig{
            .version = MXL_FABRICS_API_VERSION,
            .endpointAddress = mxlFabricsEndpointAddress{.node = "initiator", .service = "test"},
            .provider = MXL_FABRICS_PROVIDER_SHM,
            .reader = reader,
        };
        REQUIRE(mxlFabricsInitiatorSetup(initiator, &initiatorConfig, nullptr) == MXL_STATUS_OK);

        std::array<mxlFabricsTargetConfig, nbTargets> targetConfig;
        std::array<mxlFabricsTargetInfo, nbTargets> targetInfo;
        for (size_t i = 0; i < nbTargets; i++)
        {
            targetConfig[i] = mxlFabricsTargetConfig{
                .version = MXL_FABRICS_API_VERSION,
                .endpointAddress = mxlFabricsEndpointAddress{.node = "target", .service = "test"},
                .provider = MXL_FABRICS_PROVIDER_SHM,
                .writer = writer[i],
            };
            REQUIRE(mxlFabricsTargetSetup(targets[i], &targetConfig[i], nullptr, &targetInfo[i]) == MXL_STATUS_OK);

            REQUIRE(mxlFabricsInitiatorAddTarget(initiator, targetInfo[i]) == MXL_STATUS_OK);
        }

        std::uint64_t dummyIndex;
        std::size_t dummyCount;
        waitForAllConnections(
            targets,
            [&](auto& target)
            { requireProgressStatus([&]() { return mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); }, "target"); },
            [&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); });

        SECTION("non-blocking")
        {
            waitForAllTransfersStart(
                targets,
                [&](auto& target) { mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); },
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferSamples(initiator, 0, 480); });

            waitForAllTransfersCompletion(
                targets,
                [&]() { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressNonBlocking(initiator); }, "initiator"); },
                [&](auto const& target) { return mxlFabricsTargetReadSamplesNonBlocking(target, &dummyIndex, &dummyCount); });
        }

        SECTION("blocking")
        {
            waitForAllTransfersStart(
                targets,
                [&](auto& target) { mxlFabricsTargetReadSamples(target, BLOCKING_WAIT.count(), &dummyIndex, &dummyCount); },
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&]() { return mxlFabricsInitiatorTransferSamples(initiator, 0, 480); });

            waitForAllTransfersCompletion(
                targets,
                [&]()
                { requireProgressStatus([&]() { return mxlFabricsInitiatorMakeProgressBlocking(initiator, BLOCKING_WAIT.count()); }, "initiator"); },
                [&](auto const& target) { return mxlFabricsTargetReadSamples(target, BLOCKING_WAIT.count(), &dummyIndex, &dummyCount); });
        }

        for (size_t i = 0; i < nbTargets; i++)
        {
            mxlFabricsFreeTargetInfo(targetInfo[i]);
        }
    }

    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInitiator(fabrics, initiator) == MXL_STATUS_OK);
    for (size_t i = 0; i < nbTargets; i++)
    {
        REQUIRE(mxlReleaseFlowWriter(instance, writer[i]) == MXL_STATUS_OK);
        REQUIRE(mxlFabricsDestroyTarget(fabrics, targets[i]) == MXL_STATUS_OK);
    }
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

#ifdef MXL_FABRICS_OFI
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Fabrics: TargetInfo serialize/deserialize", "[fabrics][ofi][target-info]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    auto instance = mxlCreateInstance(domain.c_str(), "");
    mxlFabricsInstance fabrics;
    mxlFabricsTarget target;
    mxlFabricsTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, nullptr, &fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    auto flowDef = mxl::tests::readFile("../data/v210_flow.json");
    mxlFlowWriter writer;
    REQUIRE(mxlCreateFlowWriter(instance, flowDef.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_STATUS_OK);

    auto config = mxlFabricsTargetConfig{
        .version = MXL_FABRICS_API_VERSION,
        .endpointAddress = mxlFabricsEndpointAddress{.node = "127.0.0.1", .service = "0"},
        .provider = MXL_FABRICS_PROVIDER_TCP,
        .writer = writer,
    };

    // Retrieve the target info from the target setup
    REQUIRE(mxlFabricsTargetSetup(target, &config, nullptr, &targetInfo) == MXL_STATUS_OK);

    // Serialize the target info to a string
    size_t targetInfoStrSize = 0;
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, nullptr, &targetInfoStrSize) == MXL_STATUS_OK);
    auto targetInfoStr = std::string{};
    targetInfoStr.resize(targetInfoStrSize);
    REQUIRE(mxlFabricsTargetInfoToString(targetInfo, targetInfoStr.data(), &targetInfoStrSize) == MXL_STATUS_OK);

    // Deserialize the target info from the string
    mxlFabricsTargetInfo deserializedTargetInfo;
    REQUIRE(mxlFabricsTargetInfoFromString(targetInfoStr.c_str(), &deserializedTargetInfo) == MXL_STATUS_OK);

    // Now compare that the original and deserialized target info are the same
    auto targetInfoIn = ofi::TargetInfo::fromAPI(targetInfo);
    auto targetInfoOut = ofi::TargetInfo::fromAPI(deserializedTargetInfo);
    REQUIRE(*targetInfoIn == *targetInfoOut);

    // Cleanup
    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);
    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance, writer) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}
#endif
