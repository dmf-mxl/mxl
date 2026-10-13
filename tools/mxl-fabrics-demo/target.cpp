// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// Fabrics target: receives a flow from a remote initiator and writes it into a local MXL flow.
//
//   mxl-fabrics-demo-target -d /dev/shm -f flow.json -t /tmp/target.json
//
// The target creates the flow from flow.json, exposes its ring buffer to the network and writes the target info to
// /tmp/target.json. The initiator needs this target info to connect.
//
// The initiator writes the payload and the grain info directly into the local ring buffer. The target only has to
// open and commit each grain (or range of samples) to make it visible to local readers.

#include <cstdlib>
#include <string>
#include <fmt/format.h>
#include <mxl/dataformat.h>
#include <mxl/fabrics.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "common.hpp"

using namespace mxl::fabrics::demo;

namespace
{
    /// Timeout of the blocking read calls. Short enough to react to Ctrl-C.
    constexpr auto readTimeoutMs = uint16_t{200};

    /// Handles owned by the target. Each one is NULL until it has been created.
    struct Target
    {
        mxlInstance instance;            ///< The MXL instance for the domain.
        mxlFabricsInstance fabrics;      ///< The fabrics instance created from @c instance.
        mxlFlowWriter writer;            ///< Writer of the local flow that receives the data.
        mxlFlowConfigInfo flowConfig;    ///< Configuration of the local flow.
        mxlFabricsTarget target;         ///< The fabrics target.
        mxlFabricsTargetInfo targetInfo; ///< Address and memory keys of @c target, shared with the initiator.
        bool blocking;                   ///< The selected interface supports blocking reads.
    };

    /**
     * Create the flow writer and the fabrics target, then write the target info file.
     * @param[in,out] t The target. Handles created before a failure are kept so that teardown() can release them.
     * @param[in] options The command line options.
     * @return true on success.
     */
    bool setup(Target* t, Options const& options)
    {
        auto flowDef = std::string{};
        if (!readFile(options.flow.c_str(), &flowDef))
        {
            fmt::print(stderr, "cannot read flow definition '{}'\n", options.flow);
            return false;
        }

        t->instance = mxlCreateInstance(options.domain.c_str(), nullptr);
        if (t->instance == nullptr)
        {
            fmt::print(stderr, "cannot open MXL domain '{}'\n", options.domain);
            return false;
        }
        if (!check(mxlFabricsCreateInstance(t->instance, nullptr, &t->fabrics), "mxlFabricsCreateInstance"))
        {
            return false;
        }
        if (!check(mxlCreateFlowWriter(t->instance, flowDef.c_str(), nullptr, &t->writer, &t->flowConfig, nullptr), "mxlCreateFlowWriter"))
        {
            return false;
        }

        // The interface list owns the strings referenced by 'config', so it is freed only after mxlFabricsTargetSetup() has copied them.
        mxlFabricsInterfaceList* interfaces = nullptr;
        auto config = mxlFabricsTargetConfig{};
        config.version = MXL_FABRICS_API_VERSION;
        config.writer = t->writer;
        auto ok = check(selectInterface(t->fabrics, options, &interfaces, &config.interface), "selectInterface") &&
                  check(mxlFabricsCreateTarget(t->fabrics, &t->target), "mxlFabricsCreateTarget") &&
                  check(mxlFabricsTargetSetup(t->target, &config, nullptr, &t->targetInfo), "mxlFabricsTargetSetup");
        t->blocking = ((config.interface.caps.flags & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS) != 0);
        mxlFabricsFreeInterfaceList(interfaces);
        if (!ok)
        {
            return false;
        }

        // Serialize the target info. The first call returns the required size, including the terminating NUL.
        auto size = size_t{0};
        if (!check(mxlFabricsTargetInfoToString(t->targetInfo, nullptr, &size), "mxlFabricsTargetInfoToString"))
        {
            return false;
        }
        auto info = std::string(size, '\0');
        if (!check(mxlFabricsTargetInfoToString(t->targetInfo, info.data(), &size), "mxlFabricsTargetInfoToString"))
        {
            return false;
        }
        info.resize(size - 1);

        if (!writeFile(options.info.c_str(), info))
        {
            fmt::print(stderr, "cannot write target info to '{}'\n", options.info);
            return false;
        }
        return true;
    }

    /**
     * Commit every grain that the initiator has written, until Ctrl-C.
     * @param[in] t The target, after a successful setup().
     * @return true if stopped by a signal, false on error.
     */
    bool receiveGrains(Target* t)
    {
        auto counter = Counter{};
        while (g_running)
        {
            auto index = uint64_t{0};
            auto const status =
                t->blocking ? mxlFabricsTargetReadGrain(t->target, readTimeoutMs, &index) : mxlFabricsTargetReadGrainNonBlocking(t->target, &index);
            if ((status == MXL_ERR_NOT_READY) || (status == MXL_ERR_TIMEOUT) || (status == MXL_ERR_INTERRUPTED))
            {
                continue;
            }
            if (!check(status, "mxlFabricsTargetReadGrain"))
            {
                return false;
            }

            // The initiator has already written the grain info and payload into the ring buffer. Opening a grain resets
            // its valid slice count and invalid flag, so the grain info is read first and committed as written.
            auto info = mxlGrainInfo{};
            auto opened = mxlGrainInfo{};
            uint8_t* payload = nullptr;
            if (!check(mxlFlowWriterGetGrainInfo(t->writer, index, &info), "mxlFlowWriterGetGrainInfo") ||
                !check(mxlFlowWriterOpenGrain(t->writer, index, &opened, &payload), "mxlFlowWriterOpenGrain") ||
                !check(mxlFlowWriterCommitGrain(t->writer, &info), "mxlFlowWriterCommitGrain"))
            {
                return false;
            }
            countTransfer(&counter, info.validSlices, "slices");
        }
        return true;
    }

    /**
     * Commit every range of samples that the initiator has written, until Ctrl-C.
     * @param[in] t The target, after a successful setup().
     * @return true if stopped by a signal, false on error.
     */
    bool receiveSamples(Target* t)
    {
        auto counter = Counter{};
        while (g_running)
        {
            auto index = uint64_t{0};
            auto count = size_t{0};
            auto const status = t->blocking ? mxlFabricsTargetReadSamples(t->target, readTimeoutMs, &index, &count)
                                            : mxlFabricsTargetReadSamplesNonBlocking(t->target, &index, &count);
            if ((status == MXL_ERR_NOT_READY) || (status == MXL_ERR_TIMEOUT) || (status == MXL_ERR_INTERRUPTED))
            {
                continue;
            }
            if (!check(status, "mxlFabricsTargetReadSamples"))
            {
                return false;
            }

            // The samples are already in the ring buffer. Open and commit without changing them.
            auto slices = mxlMutableWrappedMultiBufferSlice{};
            if (!check(mxlFlowWriterOpenSamples(t->writer, index, count, &slices), "mxlFlowWriterOpenSamples") ||
                !check(mxlFlowWriterCommitSamples(t->writer), "mxlFlowWriterCommitSamples"))
            {
                return false;
            }
            countTransfer(&counter, count, "samples");
        }
        return true;
    }

    /**
     * Release all handles in reverse order of creation. Handles that are NULL are skipped.
     * @param[in,out] t The target.
     */
    void teardown(Target* t)
    {
        if (t->targetInfo != nullptr)
        {
            check(mxlFabricsFreeTargetInfo(t->targetInfo), "mxlFabricsFreeTargetInfo");
        }
        if (t->target != nullptr)
        {
            check(mxlFabricsDestroyTarget(t->fabrics, t->target), "mxlFabricsDestroyTarget");
        }
        if (t->writer != nullptr)
        {
            check(mxlReleaseFlowWriter(t->instance, t->writer), "mxlReleaseFlowWriter");
        }
        if (t->fabrics != nullptr)
        {
            check(mxlFabricsDestroyInstance(t->fabrics), "mxlFabricsDestroyInstance");
        }
        if (t->instance != nullptr)
        {
            check(mxlDestroyInstance(t->instance), "mxlDestroyInstance");
        }
    }
}

int main(int argc, char** argv)
{
    auto const options = parseOptions(argc, argv, "Flow definition JSON file of the flow to create", false);

    auto t = Target{};
    auto ok = setup(&t, options);
    if (ok)
    {
        auto const format = static_cast<int>(t.flowConfig.common.format);
        ok = (mxlIsDiscreteDataFormat(format) != 0) ? receiveGrains(&t) : receiveSamples(&t);
    }
    teardown(&t);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
