// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

// Fabrics initiator: reads a local MXL flow and writes it into the ring buffer of a remote target.
//
//   mxl-fabrics-demo-initiator -d /dev/shm -f <flow id> -t /tmp/target.json
//
// The initiator reads the target info written by mxl-fabrics-demo-target, connects to the target and then transfers
// every grain (or batch of samples) as soon as it is available in the local flow. Both sides must use the same provider.
//
// All fabrics operations are asynchronous. Transfers, connection setup and disconnection only complete during calls to
// mxlFabricsInitiatorMakeProgress*(), so the initiator calls it after each transfer until it returns MXL_STATUS_OK.

#include <cstdlib>
#include <algorithm>
#include <string>
#include <fmt/format.h>
#include <mxl/dataformat.h>
#include <mxl/fabrics.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "common.hpp"

using namespace mxl::fabrics::demo;

namespace
{
    /// How long to wait for new data in the local flow. Short enough to react to Ctrl-C.
    constexpr auto readTimeoutNs = uint64_t{200'000'000};

    /// Handles owned by the initiator. Each one is NULL until it has been created.
    struct Initiator
    {
        mxlInstance instance;            ///< The MXL instance for the domain.
        mxlFabricsInstance fabrics;      ///< The fabrics instance created from @c instance.
        mxlFlowReader reader;            ///< Reader of the local flow that is sent.
        mxlFlowConfigInfo flowConfig;    ///< Configuration of the local flow.
        mxlFabricsInitiator initiator;   ///< The fabrics initiator.
        mxlFabricsTargetInfo targetInfo; ///< The remote target, parsed from the target info file.
        bool targetAdded;                ///< The target was added and must be removed on teardown.
        bool blocking;                   ///< The selected interface supports blocking progress calls.
    };

    /**
     * Drive pending transfers and connection changes forward.
     * @param[in] in The initiator.
     * @return MXL_STATUS_OK if all operations have completed, MXL_ERR_NOT_READY if some are still pending, or an error.
     */
    mxlStatus makeProgress(Initiator* in)
    {
        return in->blocking ? mxlFabricsInitiatorMakeProgressBlocking(in->initiator, 10) : mxlFabricsInitiatorMakeProgressNonBlocking(in->initiator);
    }

    /**
     * Call makeProgress() until all pending operations have completed.
     * @param[in] in The initiator.
     * @param[in] timeoutMs Give up after this many milliseconds.
     * @return true if all operations have completed, false on error or timeout.
     */
    bool waitForCompletion(Initiator* in, uint64_t timeoutMs)
    {
        auto const deadline = mxlGetTime() + (timeoutMs * 1'000'000ULL);
        while (mxlGetTime() < deadline)
        {
            auto const status = makeProgress(in);
            if ((status != MXL_ERR_NOT_READY) && (status != MXL_ERR_INTERRUPTED))
            {
                return check(status, "mxlFabricsInitiatorMakeProgress");
            }
        }
        fmt::print(stderr, "timeout while waiting for the initiator to make progress\n");
        return false;
    }

    /**
     * Open the flow, create the fabrics initiator and connect it to the target.
     * @param[in,out] in The initiator. Handles created before a failure are kept so that teardown() can release them.
     * @param[in] options The command line options.
     * @return true if the initiator is connected to the target.
     */
    bool setup(Initiator* in, Options const& options)
    {
        auto info = std::string{};
        if (!readFile(options.info.c_str(), &info))
        {
            fmt::print(stderr, "cannot read target info file '{}'\n", options.info);
            return false;
        }

        in->instance = mxlCreateInstance(options.domain.c_str(), nullptr);
        if (in->instance == nullptr)
        {
            fmt::print(stderr, "cannot open MXL domain '{}'\n", options.domain);
            return false;
        }
        if (!check(mxlFabricsCreateInstance(in->instance, nullptr, &in->fabrics), "mxlFabricsCreateInstance") ||
            !check(mxlCreateFlowReader(in->instance, options.flow.c_str(), nullptr, &in->reader), "mxlCreateFlowReader") ||
            !check(mxlFlowReaderGetConfigInfo(in->reader, &in->flowConfig), "mxlFlowReaderGetConfigInfo"))
        {
            return false;
        }

        // The interface list owns the strings referenced by 'config', so it is freed only after mxlFabricsInitiatorSetup() has copied them.
        mxlFabricsInterfaceList* interfaces = nullptr;
        auto config = mxlFabricsInitiatorConfig{};
        config.version = MXL_FABRICS_API_VERSION;
        config.reader = in->reader;
        auto const ok = check(selectInterface(in->fabrics, options, &interfaces, &config.interface), "selectInterface") &&
                        check(mxlFabricsCreateInitiator(in->fabrics, &in->initiator), "mxlFabricsCreateInitiator") &&
                        check(mxlFabricsInitiatorSetup(in->initiator, &config, nullptr), "mxlFabricsInitiatorSetup");
        in->blocking = ((config.interface.caps.flags & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS) != 0);
        mxlFabricsFreeInterfaceList(interfaces);
        if (!ok)
        {
            return false;
        }

        // Adding a target only starts the connection. It is established during the following progress calls.
        if (!check(mxlFabricsTargetInfoFromString(info.c_str(), &in->targetInfo), "mxlFabricsTargetInfoFromString") ||
            !check(mxlFabricsInitiatorAddTarget(in->initiator, in->targetInfo), "mxlFabricsInitiatorAddTarget"))
        {
            return false;
        }
        in->targetAdded = true;
        return waitForCompletion(in, 5000);
    }

    /**
     * Transfer grains as they are written to the local flow, until Ctrl-C.
     * Grains are sent in batches of slices, so that the target receives the first lines of a video frame before the
     * writer has finished the whole frame.
     * @param[in] in The initiator, after a successful setup().
     * @return true if stopped by a signal, false on error.
     */
    bool sendGrains(Initiator* in)
    {
        auto const* rate = &in->flowConfig.common.grainRate;
        auto const batchSize = static_cast<uint16_t>(in->flowConfig.common.maxSyncBatchSizeHint);
        auto index = mxlGetCurrentIndex(rate);
        auto startSlice = uint16_t{0};
        auto endSlice = batchSize;
        auto counter = Counter{};

        while (g_running)
        {
            // Wait until at least 'endSlice' slices of the grain are valid.
            auto info = mxlGrainInfo{};
            uint8_t* payload = nullptr;
            auto status = mxlFlowReaderGetGrainSlice(in->reader, index, endSlice, readTimeoutNs, &info, &payload);
            if (status == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
            {
                // The grain was overwritten before we read it. Skip ahead to the current grain.
                index = mxlGetCurrentIndex(rate);
                startSlice = 0;
                endSlice = batchSize;
                continue;
            }
            if ((status == MXL_ERR_OUT_OF_RANGE_TOO_EARLY) || (status == MXL_ERR_TIMEOUT))
            {
                continue;
            }
            if (!check(status, "mxlFlowReaderGetGrainSlice"))
            {
                return false;
            }

            // For an invalid grain only the grain info is sent, without payload.
            auto const invalid = ((info.flags & MXL_GRAIN_FLAG_INVALID) != 0);
            auto const firstSlice = invalid ? uint16_t{0} : startSlice;
            auto const lastSlice = invalid ? uint16_t{0} : info.validSlices;
            status = mxlFabricsInitiatorTransferGrain(in->initiator, index, firstSlice, lastSlice);
            if (status == MXL_ERR_NOT_READY)
            {
                // The target is not connected at the moment. Make progress and retry the same slices.
                static_cast<void>(makeProgress(in));
                continue;
            }
            if (!check(status, "mxlFabricsInitiatorTransferGrain") || !waitForCompletion(in, 2000))
            {
                return false;
            }
            countTransfer(&counter, lastSlice - firstSlice, "slices");

            if (!invalid && (info.validSlices < info.totalSlices))
            {
                // Only a part of the grain was sent. Continue with the next batch of slices of the same grain.
                startSlice = info.validSlices;
                endSlice = static_cast<uint16_t>(std::min<uint32_t>(startSlice + batchSize, info.totalSlices));
                continue;
            }
            index += 1;
            startSlice = 0;
            endSlice = batchSize;
        }
        return true;
    }

    /**
     * Transfer samples in batches as they are written to the local flow, until Ctrl-C.
     * @param[in] in The initiator, after a successful setup().
     * @return true if stopped by a signal, false on error.
     */
    bool sendSamples(Initiator* in)
    {
        auto const batchSize = size_t{in->flowConfig.common.maxSyncBatchSizeHint};
        auto runtime = mxlFlowRuntimeInfo{};
        if (!check(mxlFlowReaderGetRuntimeInfo(in->reader, &runtime), "mxlFlowReaderGetRuntimeInfo"))
        {
            return false;
        }
        // A sample range is identified by the index of its last sample, so the first batch ends at the current head.
        auto index = runtime.headIndex;
        auto counter = Counter{};

        while (g_running)
        {
            auto slices = mxlWrappedMultiBufferSlice{};
            auto status = mxlFlowReaderGetSamples(in->reader, index, batchSize, readTimeoutNs, &slices);
            if (status == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
            {
                // The samples were overwritten before we read them. Skip ahead to the current head.
                if (!check(mxlFlowReaderGetRuntimeInfo(in->reader, &runtime), "mxlFlowReaderGetRuntimeInfo"))
                {
                    return false;
                }
                index = runtime.headIndex;
                continue;
            }
            if (status == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
            {
                continue;
            }
            if (!check(status, "mxlFlowReaderGetSamples"))
            {
                return false;
            }

            status = mxlFabricsInitiatorTransferSamples(in->initiator, index, batchSize);
            if (status == MXL_ERR_NOT_READY)
            {
                // The target is not connected at the moment. Make progress and retry the same samples.
                static_cast<void>(makeProgress(in));
                continue;
            }
            if (!check(status, "mxlFabricsInitiatorTransferSamples") || !waitForCompletion(in, 2000))
            {
                return false;
            }
            countTransfer(&counter, batchSize, "samples");
            index += batchSize;
        }
        return true;
    }

    /**
     * Disconnect from the target and release all handles in reverse order of creation. Handles that are NULL are skipped.
     * @param[in,out] in The initiator.
     */
    void teardown(Initiator* in)
    {
        if (in->targetAdded && check(mxlFabricsInitiatorRemoveTarget(in->initiator, in->targetInfo), "mxlFabricsInitiatorRemoveTarget"))
        {
            // Let the in-flight transfers and the disconnection complete.
            static_cast<void>(waitForCompletion(in, 1000));
        }
        if (in->targetInfo != nullptr)
        {
            check(mxlFabricsFreeTargetInfo(in->targetInfo), "mxlFabricsFreeTargetInfo");
        }
        if (in->initiator != nullptr)
        {
            check(mxlFabricsDestroyInitiator(in->fabrics, in->initiator), "mxlFabricsDestroyInitiator");
        }
        if (in->reader != nullptr)
        {
            check(mxlReleaseFlowReader(in->instance, in->reader), "mxlReleaseFlowReader");
        }
        if (in->fabrics != nullptr)
        {
            check(mxlFabricsDestroyInstance(in->fabrics), "mxlFabricsDestroyInstance");
        }
        if (in->instance != nullptr)
        {
            check(mxlDestroyInstance(in->instance), "mxlDestroyInstance");
        }
    }
}

int main(int argc, char** argv)
{
    auto const options = parseOptions(argc, argv, "ID of the local flow to send");

    auto in = Initiator{};
    auto ok = setup(&in, options);
    if (ok)
    {
        auto const format = static_cast<int>(in.flowConfig.common.format);
        ok = (mxlIsDiscreteDataFormat(format) != 0) ? sendGrains(&in) : sendSamples(&in);
    }
    teardown(&in);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
