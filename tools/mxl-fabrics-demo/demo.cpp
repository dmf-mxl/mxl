// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <type_traits>
#include <uuid.h>
#include <sys/types.h>
#include <CLI/CLI.hpp>
#include <mxl-internal/FlowParser.hpp>
#include <mxl-internal/Logging.hpp>
#include <mxl/fabrics.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl/dataformat.h"
#include "mxl/flowinfo.h"
#include "../../lib/fabrics/ofi/src/internal/Base64.hpp"

/*
    Example how to use:

        1- Start a target: ./mxl-fabrics-demo -d <tmpfs folder> -f <NMOS JSON File> --service 1234
        2- Paste the target info that gets printed in stdout to the --target-info argument of the initiator.
        3- Start a sender: ./mxl-fabrics-demo -i -d <tmpfs folder> -f <test source flow uuid> --service 1234 --target-info <targetInfo>

    The best available interface is selected automatically (EFA > VERBS > TCP > SHM).
    Use --node to bind to a specific address.
*/

std::sig_atomic_t volatile g_exit_requested = 0;

struct InterfaceSelection
{
    mxlFabricsProvider provider;
    mxlFabricsInterfaceCaps caps;
    std::optional<std::string> node;
    std::optional<std::string> service;
};

struct Config
{
    std::string domain;
    mxl::lib::FlowParser const& flowParser;
    InterfaceSelection interface;
};

void signal_handler(int)
{
    g_exit_requested = 1;
}

std::string capabilitiesString(std::uint64_t caps)
{
    auto resultLength = std::size_t{0};
    auto capStrings = std::vector<std::string>{};
    if (caps & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS)
    {
        resultLength += capStrings.emplace_back("BLOCKING_OPERATIONS").size();
    }
    if (caps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE)
    {
        resultLength += capStrings.emplace_back("REMOTE_WRITE").size();
    }
    if (caps & MXL_FABRICS_IFACE_CAP_SEND_RECEIVE)
    {
        resultLength += capStrings.emplace_back("SEND_RECEIVE").size();
    }
    if (capStrings.empty())
    {
        resultLength += capStrings.emplace_back("<none>").size();
    }

    resultLength += (capStrings.size() - 1); // separating '|' chars

    auto result = capStrings.front();
    result.reserve(resultLength + (capStrings.size() - 1));
    for (auto it = capStrings.begin() + 1; it != capStrings.end(); ++it)
    {
        result.push_back('|');
        result.append(*it);
    }

    return result;
}

std::string providerName(mxlFabricsProvider provider)
{
    auto size = std::size_t{0};
    mxlFabricsProviderToString(provider, nullptr, &size);
    auto result = std::string(size, '\0');
    mxlFabricsProviderToString(provider, result.data(), &size);
    if (!result.empty() && (result.back() == '\0'))
    {
        result.pop_back();
    }
    return result;
}

int providerPriority(mxlFabricsProvider provider)
{
    switch (provider)
    {
        case MXL_FABRICS_PROVIDER_EFA:   return 4;
        case MXL_FABRICS_PROVIDER_VERBS: return 3;
        case MXL_FABRICS_PROVIDER_TCP:   return 2;
        case MXL_FABRICS_PROVIDER_SHM:   return 1;
        default:                         return 0;
    }
}

InterfaceSelection selectInterface(mxlFabricsInstance instance, std::optional<std::string> const& node, std::optional<std::string> const& service,
    std::optional<mxlFabricsProvider> provider = std::nullopt)
{
    auto list = std::add_pointer_t<mxlFabricsInterfaceList>{nullptr};
    auto query = mxlFabricsInterfaceConfig{};
    query.version = MXL_FABRICS_API_VERSION;
    if (node)
    {
        query.address.node = node->c_str();
    }
    if (provider)
    {
        query.provider = *provider;
    }

    auto filtering = node || provider;
    auto status = mxlFabricsGetInterfaces(instance, filtering ? &query : nullptr, &list);
    if ((status != MXL_STATUS_OK) || (list == nullptr))
    {
        MXL_ERROR("Failed to query interfaces (mxlStatus = {})", static_cast<int>(status));
        std::exit(status);
    }

    auto best = std::add_pointer_t<mxlFabricsInterfaceConfig const>{nullptr};
    for (auto* entry = list; entry != nullptr; entry = entry->next)
    {
        if (!best || (providerPriority(entry->interface.provider) > providerPriority(best->provider)))
        {
            best = &entry->interface;
        }
    }

    if (!best)
    {
        MXL_ERROR("No matching interfaces found");
        mxlFabricsFreeInterfaceList(list);
        std::exit(MXL_ERR_NO_FABRIC);
    }

    auto result = InterfaceSelection{
        .provider = best->provider,
        .caps = best->caps,
        .node = node ? node : (best->address.node ? std::optional<std::string>(best->address.node) : std::nullopt),
        .service = service,
    };

    mxlFabricsFreeInterfaceList(list);
    return result;
}

class AppInitator
{
public:
    AppInitator(Config config)
        : _config(std::move(config))
    {}

    ~AppInitator()
    {
        mxlStatus status;

        if (_initiator && _targetInfo && _targetAdded)
        {
            // We're done let's remove the target
            status = mxlFabricsInitiatorRemoveTarget(_initiator, _targetInfo);
            if (status == MXL_STATUS_OK)
            {
                // Wait for graceful shutdown of the connection and for all transfers to complete before exiting
                status = mxlFabricsInitiatorMakeProgressBlocking(_initiator, 500);
                if (status != MXL_STATUS_OK)
                {
                    MXL_ERROR("Failed to wait in time for remove target to complete with status '{}'", static_cast<int>(status));
                }
            }
            else
            {
                MXL_ERROR("Failed to remove target with status '{}'", static_cast<int>(status));
            }
        }

        if (_targetInfo != nullptr)
        {
            if (status = mxlFabricsFreeTargetInfo(_targetInfo); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to free target info with status '{}'", static_cast<int>(status));
            }
        }

        if (_initiator != nullptr)
        {
            if (status = mxlFabricsDestroyInitiator(_fabricsInstance, _initiator); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to destroy fabrics initiator with status '{}'", static_cast<int>(status));
            }
        }

        if (_fabricsInstance != nullptr)
        {
            if (status = mxlFabricsDestroyInstance(_fabricsInstance); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to destroy fabrics instance with status '{}'", static_cast<int>(status));
            }
        }

        if (_reader != nullptr)
        {
            if (status = mxlReleaseFlowReader(_instance, _reader); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to release flow writer with status '{}'", static_cast<int>(status));
            }
        }

        if (_instance != nullptr)
        {
            if (status = mxlDestroyInstance(_instance); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to destroy instance with status '{}'", static_cast<int>(status));
            }
        }
    }

    mxlStatus setup(std::string const& targetInfoStr)
    {
        _instance = mxlCreateInstance(_config.domain.c_str(), "");
        if (_instance == nullptr)
        {
            MXL_ERROR("Failed to create MXL instance");
            return MXL_ERR_INVALID_ARG;
        }

        auto status = mxlFabricsCreateInstance(_instance, nullptr, &_fabricsInstance);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create fabrics instance with status '{}'", static_cast<int>(status));
            return status;
        }

        // Create a flow reader for the given flow id.
        status = mxlCreateFlowReader(_instance, uuids::to_string(_config.flowParser.getId()).c_str(), "", &_reader);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create flow reader with status '{}'", static_cast<int>(status));
            return status;
        }

        status = mxlFabricsCreateInitiator(_fabricsInstance, &_initiator);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create fabrics initiator with status '{}'", static_cast<int>(status));
            return status;
        }

        // clang-format off
        mxlFabricsInitiatorConfig initiatorConfig = {
            .version = MXL_FABRICS_API_VERSION,
            .interface = {
                .version = MXL_FABRICS_API_VERSION,
                .provider = _config.interface.provider,
                .caps = _config.interface.caps,
                .address = {
                    .node = _config.interface.node ? _config.interface.node->c_str() : nullptr,
                    .service = _config.interface.service ? _config.interface.service->c_str() : nullptr,
                },
                .attr = nullptr,
            },
            .reader = _reader,
        };
        // clang-format on

        status = mxlFabricsInitiatorSetup(_initiator, &initiatorConfig, nullptr);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to setup fabrics initiator with status '{}'", static_cast<int>(status));
            return status;
        }

        status = mxlFabricsTargetInfoFromString(targetInfoStr.c_str(), &_targetInfo);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to parse target info string with status '{}'", static_cast<int>(status));
            return status;
        }

        status = mxlFabricsInitiatorAddTarget(_initiator, _targetInfo);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to add target with status '{}'", static_cast<int>(status));
            return status;
        }
        _targetAdded = true;

        do
        {
            status = makeProgress(std::chrono::milliseconds(250));
            if (status == MXL_ERR_INTERRUPTED)
            {
                return MXL_STATUS_OK;
            }

            if (status != MXL_ERR_NOT_READY && status != MXL_STATUS_OK)
            {
                return status;
            }
        }
        while (status == MXL_ERR_NOT_READY);

        return MXL_STATUS_OK;
    }

    mxlStatus run()
    {
        mxlFlowConfigInfo configInfo;
        auto status = mxlFlowReaderGetConfigInfo(_reader, &configInfo);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get flow info with status '{}'", static_cast<int>(status));
            return status;
        }

        if (mxlIsDiscreteDataFormat(static_cast<int>(configInfo.common.format)))
        {
            status = runDiscrete(configInfo);
        }
        else if (mxlIsContinuousDataFormat(static_cast<int>(configInfo.common.format)))
        {
            status = runContinuous(configInfo);
        }
        else
        {
            MXL_ERROR("Unsupported data format {}", configInfo.common.format);
            return MXL_ERR_INVALID_ARG;
        }

        return status;
    }

    mxlStatus runDiscrete(mxlFlowConfigInfo const& configInfo)
    {
        // Extract the FlowInfo structure.
        std::uint16_t slicesPerBatch = configInfo.common.maxSyncBatchSizeHint;
        MXL_INFO("Using batch size of {} slices", slicesPerBatch);

        mxlGrainInfo grainInfo;
        std::uint8_t* payload;
        std::uint16_t startSlice = 0;
        std::uint16_t endSlice = slicesPerBatch;

        uint64_t grainIndex = mxlGetCurrentIndex(&configInfo.common.grainRate);

        while (!g_exit_requested)
        {
            auto status = mxlFlowReaderGetGrainSlice(_reader, grainIndex, endSlice, 200000000, &grainInfo, &payload);
            if (status == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
            {
                // We are too late.. time travel!
                grainIndex = mxlGetCurrentIndex(&configInfo.common.grainRate);
                continue;
            }
            else if (status == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
            { // NOLINT(bugprone-branch-clone): Repeated for clarity.
                // We are too early somehow.. retry the same grain later.
                continue;
            }
            else if (status == MXL_ERR_TIMEOUT)
            {
                // No grains available before a timeout was triggered.. most likely a problem upstream.
                continue;
            }
            else if (status != MXL_STATUS_OK)
            {
                // Something  unexpected occured, not much we can do, but log and retry
                MXL_ERROR("Missed grain {}, err : {}", grainIndex, (int)status);

                continue;
            }

            if (grainInfo.flags & MXL_GRAIN_FLAG_INVALID)
            {
                // If we've got an invalid grain, do not waster bandwidth, transfer only the grain header and go to the next grain.
                status = mxlFabricsInitiatorTransferGrain(_initiator, grainIndex, 0, 0);
                ++grainIndex;
                continue;
            }

            // Okay the grain is ready, we can transfer it to the targets.
            status = mxlFabricsInitiatorTransferGrain(_initiator, grainIndex, startSlice, grainInfo.validSlices);
            if (status == MXL_ERR_NOT_READY)
            {
                continue;
            }
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to transfer grain with status '{}'", static_cast<int>(status));
                return status;
            }

            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            do
            {
                status = makeProgress(std::chrono::milliseconds(10));
                if (status == MXL_ERR_INTERRUPTED)
                {
                    return MXL_STATUS_OK;
                }

                if (status != MXL_ERR_NOT_READY && status != MXL_STATUS_OK)
                {
                    return status;
                }
            }
            while (status == MXL_ERR_NOT_READY && deadline > std::chrono::steady_clock::now());
            MXL_DEBUG("Transferred grain index={} slices {}-{}", grainIndex, startSlice, grainInfo.validSlices);

            if (grainInfo.validSlices != grainInfo.totalSlices)
            {
                // partial commit, we will need to work on the same grain again.
                startSlice = grainInfo.validSlices;
                endSlice = std::min<std::uint16_t>(startSlice + slicesPerBatch, _config.flowParser.getTotalPayloadSlices());
                continue;
            }

            // If we get here, we have transfered the grain completely, we can work on the next grain.
            startSlice = 0;
            endSlice = slicesPerBatch;
            ++grainIndex;
        }

        return MXL_STATUS_OK;
    }

    mxlStatus runContinuous(mxlFlowConfigInfo const& configInfo)
    {
        mxlWrappedMultiBufferSlice payload;

        mxlFlowRuntimeInfo runtimeInfo;
        mxlFlowReaderGetRuntimeInfo(_reader, &runtimeInfo);
        std::uint64_t headIndex = runtimeInfo.headIndex;
        auto previousHeadIndex = std::uint64_t{0};
        std::size_t batchSize = configInfo.common.maxSyncBatchSizeHint;
        MXL_INFO("batch size in samples: {}", batchSize);

        while (!g_exit_requested)
        {
            auto status = mxlFlowReaderGetSamplesNonBlocking(_reader, headIndex, batchSize, &payload);
            if (status == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
            {
                // We are too late.. time travel to the last headIndex commited!
                mxlFlowReaderGetRuntimeInfo(_reader, &runtimeInfo);
                previousHeadIndex = headIndex;
                headIndex = runtimeInfo.headIndex;
                MXL_INFO("Too late! previous headIndex={}  new headIndex={}", previousHeadIndex, headIndex);
                continue;
            }
            if (status == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
            {
                // We are too early somehow.. retry the same samples later.
                continue;
            }
            if (status == MXL_ERR_TIMEOUT)
            {
                // No grains available before a timeout was triggered.. most likely a problem upstream.
                continue;
            }
            if (status != MXL_STATUS_OK)
            {
                // Something  unexpected occured, not much we can do, but log and retry
                MXL_ERROR("Missed sample index {}, err : {}", headIndex, (int)status);

                continue;
            }

            // Okay the samples are ready, we can transfer it to the targets.
            status = mxlFabricsInitiatorTransferSamples(_initiator, headIndex, batchSize);
            if (status == MXL_ERR_NOT_READY)
            {
                status = mxlFabricsInitiatorMakeProgressNonBlocking(_initiator);
                MXL_WARN("Targets not ready for transfer, makeProgress returned '{}'", static_cast<int>(status));
                continue;
            }
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to transfer grain with status '{}'", static_cast<int>(status));
                return status;
            }

            // wait for completion
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            do
            {
                status = makeProgress(std::chrono::milliseconds(10));
                if (status == MXL_ERR_INTERRUPTED)
                {
                    return MXL_STATUS_OK;
                }

                if (status != MXL_ERR_NOT_READY && status != MXL_STATUS_OK)
                {
                    return status;
                }
            }
            while (status == MXL_ERR_NOT_READY && deadline > std::chrono::steady_clock::now());
            MXL_DEBUG("Transferred samples headIndex={} count={}", headIndex, batchSize);

            previousHeadIndex = headIndex;

            // If we get here, we have transfered the samples, we can work on the next samples.
            headIndex += batchSize;
            mxlSleepForNs(mxlGetNsUntilIndex(headIndex, &configInfo.common.grainRate));
        }

        return MXL_STATUS_OK;
    }

private:
    mxlStatus makeProgress(std::chrono::steady_clock::duration timeout)
    {
        if (_config.interface.caps.flags & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS)
        {
            return mxlFabricsInitiatorMakeProgressBlocking(_initiator, std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
        }
        else
        {
            return mxlFabricsInitiatorMakeProgressNonBlocking(_initiator);
        }
    }

private:
    Config _config;

    mxlInstance _instance = nullptr;
    mxlFabricsInstance _fabricsInstance = nullptr;
    mxlFlowReader _reader = nullptr;
    mxlFabricsInitiator _initiator = nullptr;
    mxlFabricsTargetInfo _targetInfo = nullptr;
    bool _targetAdded = false;
};

class AppTarget
{
public:
    AppTarget(Config config)
        : _config(std::move(config))
    {}

    ~AppTarget()
    {
        mxlStatus status;

        if (_targetInfo != nullptr)
        {
            if (status = mxlFabricsFreeTargetInfo(_targetInfo); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to free target info with status '{}'", static_cast<int>(status));
            }
        }

        if (_target != nullptr)
        {
            if (status = mxlFabricsDestroyTarget(_fabricsInstance, _target); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to destroy target with status '{}'", static_cast<int>(status));
            }
        }

        if (_fabricsInstance != nullptr)
        {
            if (status = mxlFabricsDestroyInstance(_fabricsInstance); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to destroy fabrics instance with status '{}'", static_cast<int>(status));
            }
        }

        if (_writer != nullptr)
        {
            if (status = mxlReleaseFlowWriter(_instance, _writer); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to release flow writer with status '{}'", static_cast<int>(status));
            }
        }

        if (_instance != nullptr)
        {
            if (status = mxlDestroyInstance(_instance); status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to destroy instance with status '{}'", static_cast<int>(status));
            }
        }
    }

    mxlStatus setup(std::string const& flowDescriptor, std::string const& flowOptions)
    {
        _instance = mxlCreateInstance(_config.domain.c_str(), "");
        if (_instance == nullptr)
        {
            MXL_ERROR("Failed to create MXL instance");
            return MXL_ERR_INVALID_ARG;
        }

        auto status = mxlFabricsCreateInstance(_instance, nullptr, &_fabricsInstance);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create fabrics instance with status '{}'", static_cast<int>(status));
            return status;
        }

        bool flowCreated = false;
        status = mxlCreateFlowWriter(_instance, flowDescriptor.c_str(), flowOptions.c_str(), &_writer, &_configInfo, &flowCreated);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create flow writer with status '{}'", static_cast<int>(status));
            return status;
        }
        if (!flowCreated)
        {
            MXL_WARN("Reusing existing flow");
        }

        status = mxlFabricsCreateTarget(_fabricsInstance, &_target);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create fabrics target with status '{}'", static_cast<int>(status));
            return status;
        }

        // clang-format off
        mxlFabricsTargetConfig targetConfig = {
            .version = MXL_FABRICS_API_VERSION,
            .interface = {
                .version = MXL_FABRICS_API_VERSION,
                .provider = _config.interface.provider,
                .caps = _config.interface.caps,
                .address = {
                    .node = _config.interface.node ? _config.interface.node->c_str() : nullptr,
                    .service = _config.interface.service ? _config.interface.service->c_str() : nullptr,
                },
                .attr = nullptr,
            },
            .writer = _writer,
        };
        // clang-format on
        status = mxlFabricsTargetSetup(_target, &targetConfig, nullptr, &_targetInfo);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to setup fabrics target with status '{}'", static_cast<int>(status));
            return status;
        }

        return MXL_STATUS_OK;
    }

    mxlStatus printInfo(std::string const& targetInfoFile = {})
    {
        auto targetInfoStr = std::string{};
        size_t targetInfoStrSize;

        auto status = mxlFabricsTargetInfoToString(_targetInfo, nullptr, &targetInfoStrSize);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get target info string size with status '{}'", static_cast<int>(status));
            return status;
        }
        targetInfoStr.resize(targetInfoStrSize);

        status = mxlFabricsTargetInfoToString(_targetInfo, targetInfoStr.data(), &targetInfoStrSize);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to convert target info to string with status '{}'", static_cast<int>(status));
            return status;
        }
        targetInfoStr.pop_back();

        MXL_INFO("Target info:  {}", base64::to_base64(targetInfoStr));

        if (!targetInfoFile.empty())
        {
            auto of = std::ofstream{targetInfoFile};
            of << targetInfoStr;
            if (of.fail())
            {
                MXL_ERROR("Failed to write target info to '{}'", targetInfoFile);
                return MXL_ERR_INTERNAL;
            }
        }

        return MXL_STATUS_OK;
    }

    mxlStatus run()
    {
        mxlStatus status;

        if (mxlIsDiscreteDataFormat(static_cast<int>(_configInfo.common.format)))
        {
            status = runDiscrete();
        }
        else if (mxlIsContinuousDataFormat(static_cast<int>(_configInfo.common.format)))
        {
            status = runContinuous();
        }
        else
        {
            MXL_ERROR("Unsupported data format {}", _configInfo.common.format);
            return MXL_ERR_INVALID_ARG;
        }

        return status;
    }

    mxlStatus runDiscrete()
    {
        mxlGrainInfo grainInfo;
        std::uint64_t grainIndex = 0;
        std::uint8_t* dummyPayload;
        mxlStatus status;

        // This demo is a pure proxy: it only commits each grain and does no heavy per-grain processing, so draining and committing one grain
        // per iteration keeps the completion queue empty and is all that is required here. Applications that do slower per-grain work (decoding,
        // writing to disk, updating statistics) should instead size the target completion queue via the "cqDepth" setup option and adopt the
        // two-phase drain pattern. See the "Receiving grains" section of docs/Fabrics.md.
        while (!g_exit_requested)
        {
            status = targetReadGrain(&grainIndex, std::chrono::milliseconds(200));
            if (status == MXL_ERR_TIMEOUT)
            {
                // No completion before a timeout was triggered, most likely a problem upstream.
                MXL_WARN("wait for new grain timeout, most likely there is a problem upstream.");
                continue;
            }
            else if (status == MXL_ERR_NOT_READY)
            {
                continue;
            }
            else if (status == MXL_ERR_INTERRUPTED)
            {
                return MXL_STATUS_OK;
            }
            else if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to wait for grain with status '{}'", static_cast<int>(status));
                return status;
            }

            // Here we open so that we can commit, we are not going to modify the grain as it was already modified by the initiator.
            status = mxlFlowWriterOpenGrain(_writer, grainIndex, &grainInfo, &dummyPayload);
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to open grain with status '{}'", static_cast<int>(status));
                return status;
            }

            // GrainInfo and media payload was already written by the remote endpoint, we simply commit!.
            status = mxlFlowWriterCommitGrain(_writer, &grainInfo);
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to commit grain with status '{}'", static_cast<int>(status));
                return status;
            }

            MXL_DEBUG("Committed grain with index={} current index={} validSlices={} flags={}",
                grainIndex,
                mxlGetCurrentIndex(&_configInfo.common.grainRate),
                grainInfo.validSlices,
                grainInfo.flags);
        }

        return MXL_STATUS_OK;
    }

    mxlStatus runContinuous()
    {
        size_t count;
        uint64_t headIndex;
        mxlMutableWrappedMultiBufferSlice dummySlice;

        mxlStatus status;

        while (!g_exit_requested)
        {
            status = mxlFabricsTargetReadSamples(_target, 200, &headIndex, &count);
            if (status == MXL_ERR_TIMEOUT)
            {
                // No completion before a timeout was triggered, most likely a problem upstream.
                MXL_WARN("wait for new sample timeout, most likely there is a problem upstream.");
                continue;
            }
            else if (status == MXL_ERR_NOT_READY)
            {
                continue;
            }
            else if (status == MXL_ERR_INTERRUPTED)
            {
                return MXL_STATUS_OK;
            }
            else if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to wait for samples with status '{}'", static_cast<int>(status));
                return status;
            }

            // Here we open so that we can commit, we are not going to modify the grain as it was already modified by the initiator.
            status = mxlFlowWriterOpenSamples(_writer, headIndex, count, &dummySlice);
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to open grain with status '{}'", static_cast<int>(status));
                return status;
            }

            // GrainInfo and media payload was already written by the remote endpoint, we simply commit!.
            status = mxlFlowWriterCommitSamples(_writer);
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to commit grain with status '{}'", static_cast<int>(status));
                return status;
            }

            MXL_DEBUG("Cmomitted samples with head index={} count={}", headIndex, count);
        }

        return MXL_STATUS_OK;
    }

private:
    mxlStatus targetReadGrain(std::uint64_t* grainIndex, std::chrono::steady_clock::duration timeout)
    {
        if (_config.interface.caps.flags & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS)
        {
            return mxlFabricsTargetReadGrain(_target, std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count(), grainIndex);
        }
        else
        {
            return mxlFabricsTargetReadGrainNonBlocking(_target, grainIndex);
        }
    }

private:
    Config _config;

    mxlInstance _instance = nullptr;
    mxlFabricsInstance _fabricsInstance = nullptr;
    mxlFlowWriter _writer = nullptr;
    mxlFabricsTarget _target = nullptr;
    mxlFabricsTargetInfo _targetInfo = nullptr;
    mxlFlowConfigInfo _configInfo{};
};

int main(int argc, char** argv)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

    CLI::App app("mxl-fabrics-demo");

    std::string domain;
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    std::string flowConf;
    app.add_option("-f, --flow",
        flowConf,
        "The flow ID when used as an initiator. The json file which contains the NMOS Flow configuration when used as a target.");

    std::string flowOptionsFile;
    app.add_option("--flow-options", flowOptionsFile, "Flow options file. (Only used when invoking a Target)");

    bool runAsInitiator = false;
    auto runAsInitiatorOpt = app.add_flag("-i,--initiator",
        runAsInitiator,
        "Run as an initiator (flow reader + fabrics initiator). If not set, run as a receiver (fabrics target + flow writer).");
    runAsInitiatorOpt->default_val(false);

    std::string node;
    app.add_option(
        "-n,--node", node, "Filter interface selection by node address. If not set, the best available interface is chosen automatically.");

    std::string service;
    app.add_option("-s,--service", service, "Service identifier for the fabrics endpoint (e.g. a port number).");

    std::string provider;
    app.add_option("-p,--provider", provider, "Force a specific provider (tcp, verbs, efa, shm). Auto-selected if omitted.");

    std::string targetInfo;
    app.add_option("-t,--target-info",
        targetInfo,
        "As target: output file path for raw target info (optional, always logged as base64). "
        "As initiator: base64-encoded target info, or a path prefixed with '@' to read raw target info from a file.");

    CLI11_PARSE(app, argc, argv);

    auto* instance = mxlCreateInstance(domain.c_str(), "");
    if (instance == nullptr)
    {
        MXL_ERROR("Failed to create MXL instance");
        return MXL_ERR_INVALID_ARG;
    }

    auto optNode = node.empty() ? std::nullopt : std::optional<std::string>(node);
    auto optService = service.empty() ? std::nullopt : std::optional<std::string>(service);
    auto optProvider = std::optional<mxlFabricsProvider>{};
    if (!provider.empty())
    {
        auto parsed = mxlFabricsProvider{};
        if (mxlFabricsProviderFromString(provider.c_str(), &parsed) != MXL_STATUS_OK)
        {
            MXL_ERROR("Unknown provider '{}'", provider);
            mxlDestroyInstance(instance);
            return MXL_ERR_INVALID_ARG;
        }
        optProvider = parsed;
    }
    auto fabricsInstance = mxlFabricsInstance{nullptr};
    if (auto s = mxlFabricsCreateInstance(instance, nullptr, &fabricsInstance); s != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to create fabrics instance with status '{}'", static_cast<int>(s));
        mxlDestroyInstance(instance);
        return s;
    }

    auto selectedInterface = selectInterface(fabricsInstance, optNode, optService, optProvider);
    mxlFabricsDestroyInstance(fabricsInstance);
    mxlDestroyInstance(instance);

    MXL_INFO("Selected interface:");
    MXL_INFO("  provider:     {}", providerName(selectedInterface.provider));
    MXL_INFO("  node:         {}", selectedInterface.node.value_or("(auto)"));
    MXL_INFO("  service:      {}", selectedInterface.service.value_or("(none)"));
    MXL_INFO("  capabilities: {}", capabilitiesString(selectedInterface.caps.flags));
    MXL_INFO("  max message:  {} bytes", selectedInterface.caps.maxMessageSize);

    auto status = mxlStatus{};
    if (runAsInitiator)
    {
        MXL_INFO("Running as initiator");
        auto fileName = fmt::format("{}/{}.mxl-flow/flow_def.json", domain, flowConf);
        std::ifstream file(fileName, std::ios::in | std::ios::binary);
        if (!file)
        {
            MXL_ERROR("Failed to open file: '{}'", fileName);
            return MXL_ERR_INVALID_ARG;
        }
        std::string flowDescriptor{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        mxl::lib::FlowParser descriptorParser{flowDescriptor};

        auto resolvedTargetInfo = std::string{};
        if (!targetInfo.empty() && targetInfo.starts_with('@'))
        {
            // Read raw target info from file
            auto filePath = std::string{targetInfo.begin() + 1, targetInfo.end()};
            auto of = std::ifstream{filePath};
            if (of.fail())
            {
                MXL_ERROR("Failed to open target info file '{}'", filePath);
                return MXL_ERR_INTERNAL;
            }
            resolvedTargetInfo = std::string{std::istreambuf_iterator<char>{of}, std::istreambuf_iterator<char>{}};
        }
        else
        {
            resolvedTargetInfo = base64::from_base64(targetInfo);
        }

        auto app = AppInitator{
            Config{
                   .domain = domain,
                   .flowParser = descriptorParser,
                   .interface = selectedInterface,
                   },
        };

        if (status = app.setup(resolvedTargetInfo); status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to setup initiator with status '{}'", static_cast<int>(status));
            return status;
        }

        if (status = app.run(); status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to run initiator with status '{}'", static_cast<int>(status));
            return status;
        }
    }
    else
    {
        MXL_INFO("Running as target");

        std::ifstream file(flowConf, std::ios::in | std::ios::binary);
        if (!file)
        {
            MXL_ERROR("Failed to open file: '{}'", flowConf);
            return MXL_ERR_INVALID_ARG;
        }
        std::string flowDescriptor{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        mxl::lib::FlowParser descriptorParser{flowDescriptor};

        auto flowId = uuids::to_string(descriptorParser.getId());

        std::string flowOptions;
        if (!flowOptionsFile.empty())
        {
            std::ifstream optionFile(flowOptionsFile, std::ios::in | std::ios::binary);
            if (!optionFile)
            {
                MXL_ERROR("Failed to open file: '{}'", flowOptionsFile);
                return EXIT_FAILURE;
            }
            flowOptions = {std::istreambuf_iterator<char>(optionFile), std::istreambuf_iterator<char>()};
        }

        auto app = AppTarget{
            Config{
                   .domain = domain,
                   .flowParser = descriptorParser,
                   .interface = selectedInterface,
                   },
        };

        if (status = app.setup(flowDescriptor, flowOptions); status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to setup target with status '{}'", static_cast<int>(status));
            return status;
        }

        auto targetInfoPath = targetInfo.empty() || targetInfo[0] != '@' ? targetInfo : std::string{targetInfo.begin() + 1, targetInfo.end()};
        if (status = app.printInfo(targetInfoPath); status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to print target info with status '{}'", static_cast<int>(status));
            return status;
        }

        if (status = app.run(); status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to run target with status '{}'", static_cast<int>(status));
            return status;
        }
    }

    return MXL_STATUS_OK;
}
