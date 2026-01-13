// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <uuid.h>
#include <sys/types.h>
#include <CLI/CLI.hpp>
#include <mxl-internal/FlowParser.hpp>
#include <mxl-internal/Logging.hpp>
#include <mxl/fabrics.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "CLI/CLI.hpp"
#include "../../lib/fabrics/ofi/src/internal/Base64.hpp"

/*
    Example how to use:

        1- Start a target: ./mxl-fabrics-demo -d <tmpfs folder> -f <NMOS JSON File> --node 2.2.2.2 --service 1234 --provider verbs
        2- Paste the target info that gets printed in stdout to the --target-info argument of the initiator.
        3- Start a sender: ./mxl-fabrics-demo -i -d <tmpfs folder> -f <test source flow uuid> --node 1.1.1.1 --service 1234 --provider verbs
   --target-info <targetInfo>
*/

std::sig_atomic_t volatile g_exit_requested = 0;

struct Config
{
    std::string domain;

    // flow configuration
    mxl::lib::FlowParser const& flowParser;

    // endpoint configuration
    std::optional<std::string> node;
    std::optional<std::string> service;
    mxlFabricsProvider provider;
};

void signal_handler(int)
{
    g_exit_requested = 1;
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

    mxlStatus setup(std::string targetInfoStr)
    {
        _instance = mxlCreateInstance(_config.domain.c_str(), "");
        if (_instance == nullptr)
        {
            MXL_ERROR("Failed to create MXL instance");
            return MXL_ERR_INVALID_ARG;
        }

        auto status = mxlFabricsCreateInstance(_instance, &_fabricsInstance);
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

        mxlFabricsRegions regions;
        status = mxlFabricsRegionsForFlowReader(_reader, &regions);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get flow memory region with status '{}'", static_cast<int>(status));
            return status;
        }

        mxlFabricsInitiatorConfig initiatorConfig = {
            .endpointAddress = {.node = _config.node ? _config.node.value().c_str() : nullptr,
                                .service = _config.service ? _config.service.value().c_str() : nullptr},
            .provider = _config.provider,
            .regions = regions,
            .deviceSupport = false,
        };

        status = mxlFabricsInitiatorSetup(_initiator, &initiatorConfig);
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
    { // Extract the FlowInfo structure.
        mxlFlowConfigInfo configInfo;
        auto status = mxlFlowReaderGetConfigInfo(_reader, &configInfo);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get flow info with status '{}'", static_cast<int>(status));
            return status;
        }

        std::uint16_t slicesPerBatch = configInfo.common.maxSyncBatchSizeHint;
        MXL_INFO("Using batch size of {} slices", slicesPerBatch);

        mxlGrainInfo grainInfo;
        std::uint8_t* payload;
        std::uint16_t startSlice = 0;
        std::uint16_t endSlice = slicesPerBatch;

        uint64_t grainIndex = mxlGetCurrentIndex(&configInfo.common.grainRate);

        while (!g_exit_requested)
        {
            auto ret = mxlFlowReaderGetGrainSlice(_reader, grainIndex, endSlice, 200000000, &grainInfo, &payload);
            if (ret == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
            {
                // We are too late.. time travel!
                grainIndex = mxlGetCurrentIndex(&configInfo.common.grainRate);
                continue;
            }
            else if (ret == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
            {
                // We are too early somehow.. retry the same grain later.
                continue;
            }
            else if (ret == MXL_ERR_TIMEOUT)
            {
                // No grains available before a timeout was triggered.. most likely a problem upstream.
                continue;
            }
            else if (ret != MXL_STATUS_OK)
            {
                // Something  unexpected occured, not much we can do, but log and retry
                MXL_ERROR("Missed grain {}, err : {}", grainIndex, (int)ret);

                continue;
            }

            if (grainInfo.flags & MXL_GRAIN_FLAG_INVALID)
            {
                // If we've got an invalid grain, do not waster bandwidth, transfer only the grain header and go to the next grain.
                ret = mxlFabricsInitiatorTransferGrain(_initiator, grainIndex, 0, 0);
                ++grainIndex;
                continue;
            }

            // Okay the grain is ready, we can transfer it to the targets.
            ret = mxlFabricsInitiatorTransferGrain(_initiator, grainIndex, startSlice, grainInfo.validSlices);
            if (ret == MXL_ERR_NOT_READY)
            {
                continue;
            }
            if (ret != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to transfer grain with status '{}'", static_cast<int>(ret));
                return status;
            }

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
            while (status == MXL_ERR_NOT_READY);

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

        status = mxlFabricsInitiatorRemoveTarget(_initiator, _targetInfo);
        if (status != MXL_STATUS_OK)
        {
            return status;
        }

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

private:
    mxlStatus makeProgress(std::chrono::steady_clock::duration timeout)
    {
        if (_config.provider == MXL_FABRICS_PROVIDER_EFA)
        {
            return mxlFabricsInitiatorMakeProgressNonBlocking(_initiator);
        }
        else
        {
            return mxlFabricsInitiatorMakeProgressBlocking(_initiator, std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
        }
    }

private:
    Config _config;

    mxlInstance _instance;
    mxlFabricsInstance _fabricsInstance;
    mxlFlowReader _reader;
    mxlFabricsInitiator _initiator;
    mxlFabricsTargetInfo _targetInfo;
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

        auto status = mxlFabricsCreateInstance(_instance, &_fabricsInstance);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create fabrics instance with status '{}'", static_cast<int>(status));
            return status;
        }

        mxlFlowConfigInfo configInfo;
        bool flowCreated = false;
        status = mxlCreateFlowWriter(_instance, flowDescriptor.c_str(), flowOptions.c_str(), &_writer, &configInfo, &flowCreated);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create flow writer with status '{}'", static_cast<int>(status));
            return status;
        }
        if (!flowCreated)
        {
            MXL_WARN("Reusing existing flow");
        }

        mxlFabricsRegions memoryRegions;
        status = mxlFabricsRegionsForFlowWriter(_writer, &memoryRegions);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get flow memory region with status '{}'", static_cast<int>(status));
            return status;
        }

        status = mxlFabricsCreateTarget(_fabricsInstance, &_target);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create fabrics target with status '{}'", static_cast<int>(status));
            return status;
        }

        mxlFabricsTargetConfig targetConfig = {
            .endpointAddress = {.node = _config.node ? _config.node.value().c_str() : nullptr,
                                .service = _config.service ? _config.service.value().c_str() : nullptr},
            .provider = _config.provider,
            .regions = memoryRegions,
            .deviceSupport = false,
        };
        status = mxlFabricsTargetSetup(_target, &targetConfig, &_targetInfo);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to setup fabrics target with status '{}'", static_cast<int>(status));
            return status;
        }

        status = mxlFabricsRegionsFree(memoryRegions);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to free memory regions with status '{}'", static_cast<int>(status));
            return status;
        }

        return MXL_STATUS_OK;
    }

    mxlStatus printInfo()
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

        MXL_INFO("Target info:  {}", base64::to_base64(targetInfoStr));

        return MXL_STATUS_OK;
    }

    mxlStatus run()
    {
        mxlGrainInfo grainInfo;
        uint16_t entryIndex = 0;
        uint16_t validSlices = 0;
        uint8_t* dummyPayload;
        mxlStatus status;

        while (!g_exit_requested)
        {
            status = targetReadGrain(&entryIndex, &validSlices, std::chrono::milliseconds(200));
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

            status = mxlFlowWriterGetGrainInfo(_writer, entryIndex, &grainInfo);
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to get grain info with status '{}'", static_cast<int>(status));
                return status;
            }

            std::uint64_t grainIndex = grainInfo.index;

            // Here we open so that we can commit, we are not going to modify the grain as it was already modified by the initiator.
            status = mxlFlowWriterOpenGrain(_writer, grainIndex, &grainInfo, &dummyPayload);
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to open grain with status '{}'", static_cast<int>(status));
                return status;
            }

            // Now that the grain is open, we can se the valid slices.
            grainInfo.validSlices = validSlices;

            // GrainInfo and media payload was already written by the remote endpoint, we simply commit!.
            status = mxlFlowWriterCommitGrain(_writer, &grainInfo);
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to commit grain with status '{}'", static_cast<int>(status));
                return status;
            }

            MXL_DEBUG("Comitted grain with index={} current index={} validSlices={} flags={}",
                grainIndex,
                mxlGetCurrentIndex(&_configInfo.common.grainRate),
                validSlices,
                grainInfo.flags);
        }

        return MXL_STATUS_OK;
    }

private:
    mxlStatus targetReadGrain(std::uint16_t* entryIndex, std::uint16_t* validSlices, std::chrono::steady_clock::duration timeout)
    {
        if (_config.provider == MXL_FABRICS_PROVIDER_EFA)
        {
            return mxlFabricsTargetReadGrainNonBlocking(_target, entryIndex, validSlices);
        }
        else
        {
            return mxlFabricsTargetReadGrain(
                _target, entryIndex, validSlices, std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
        }
    }

private:
    Config _config;

    mxlInstance _instance;
    mxlFabricsInstance _fabricsInstance;
    mxlFlowWriter _writer;
    mxlFabricsTarget _target;
    mxlFabricsTargetInfo _targetInfo;
    mxlFlowConfigInfo _configInfo;
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
    auto nodeOpt = app.add_option("-n,--node",
        node,
        "This corresponds to the interface identifier of the fabrics endpoint, it can also be a logical address. This can be seen as the bind "
        "address when using sockets.");
    nodeOpt->default_val("");

    std::string service;
    auto serviceOpt = app.add_option("--service",
        service,
        "This corresponds to a service identifier for the fabrics endpoint. This can be seen as the bind port when using sockets.");
    serviceOpt->default_val("");

    std::string provider;
    auto providerOpt = app.add_option("-p,--provider", provider, "The fabrics provider. One of (tcp, verbs or efa). Default is 'tcp'.");
    providerOpt->default_val("tcp");

    std::string targetInfo;
    app.add_option("--target-info",
        targetInfo,
        "The target information. This is used when configured as an initiator . This is the target information to send to."
        "You first start the target and it will print the targetInfo that you paste to this argument");

    CLI11_PARSE(app, argc, argv);

    mxlFabricsProvider mxlProvider;
    auto status = mxlFabricsProviderFromString(provider.c_str(), &mxlProvider);
    if (status != MXL_STATUS_OK)
    {
        MXL_ERROR("Failed to parse provider '{}'", provider);
        return status;
    }

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

        auto app = AppInitator{
            Config{
                   .domain = domain,
                   .flowParser = descriptorParser,
                   .node = node.empty() ? std::nullopt : std::optional<std::string>(node),
                   .service = service.empty() ? std::nullopt : std::optional<std::string>(service),
                   .provider = mxlProvider,
                   },
        };

        if (status = app.setup(base64::from_base64(targetInfo)); status != MXL_STATUS_OK)
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
                   .node = node.empty() ? std::nullopt : std::optional<std::string>(node),
                   .service = service.empty() ? std::nullopt : std::optional<std::string>(service),
                   .provider = mxlProvider,
                   },
        };

        if (status = app.setup(flowDescriptor, flowOptions); status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to setup target with status '{}'", static_cast<int>(status));
            return status;
        }

        if (status = app.printInfo(); status != MXL_STATUS_OK)
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
