// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file demo.cpp
 * @brief MXL fabrics transport demonstration - network-based MXL flow transmission
 *
 * This tool demonstrates MXL's fabrics layer, which enables zero-copy RDMA-based
 * transport of MXL flows across network fabrics (libfabric/OFI backends like
 * Ethernet TCP, InfiniBand Verbs, AWS EFA, etc.).
 *
 * The tool can run in two modes:
 *
 * TARGET MODE (Receiver):
 *   - Creates an MXL flow writer from an NMOS JSON descriptor
 *   - Sets up a fabrics target endpoint that listens for connections
 *   - Receives grains via RDMA and commits them to the local MXL flow
 *   - Prints base64-encoded target info for initiator to connect
 *
 * INITIATOR MODE (Sender):
 *   - Creates an MXL flow reader for an existing flow
 *   - Parses target info string from the command line
 *   - Connects to the remote target via fabrics
 *   - Reads grains locally and transmits them via RDMA to target
 *
 * Key features:
 *   - Zero-copy RDMA for high-performance media transport
 *   - Works with any MXL flow format (video, audio, data)
 *   - Automatic memory region registration for RDMA
 *   - Progressive grain transfer (handles partial grains)
 *   - Supports multiple libfabric providers (tcp, verbs, efa)
 *   - Graceful connection setup and teardown
 *
 * Usage (2-step process):
 *   1. Start target (receiver):
 *      ./mxl-fabrics-demo -d /tmp/domain -f flow.json \
 *                         --node 2.2.2.2 --service 1234 --provider verbs
 *      → This prints a target-info string (base64-encoded)
 *
 *   2. Start initiator (sender):
 *      ./mxl-fabrics-demo -i -d /tmp/domain -f <flow-uuid> \
 *                         --node 1.1.1.1 --service 1234 --provider verbs \
 *                         --target-info <base64-string-from-step-1>
 *      → This connects and starts transmitting grains
 *
 * Network topology:
 *   Initiator (sender)                Target (receiver)
 *   ┌─────────────────┐              ┌─────────────────┐
 *   │ MXL Flow Reader │              │ MXL Flow Writer │
 *   │       ↓         │              │       ↑         │
 *   │ Fabrics         │  ─────────→  │ Fabrics         │
 *   │ Initiator       │  RDMA/OFI    │ Target          │
 *   └─────────────────┘              └─────────────────┘
 *
 * This tool demonstrates:
 *   - MXL fabrics API usage (initiator and target setup)
 *   - RDMA memory region management
 *   - Grain transfer synchronization
 *   - Connection lifecycle management
 *   - Integration with MXL flow readers/writers
 */

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

/** @brief Global flag set by signal handler for graceful shutdown */
std::sig_atomic_t volatile g_exit_requested = 0;

/**
 * @brief Configuration parameters for initiator and target operation
 */
struct Config
{
    std::string domain;                    ///< MXL domain directory path

    // flow configuration
    std::string flowID;                    ///< Flow UUID (initiator) or NMOS JSON path (target)

    // endpoint configuration
    std::optional<std::string> node;       ///< Fabric node address (IP or interface name)
    std::optional<std::string> service;    ///< Fabric service identifier (port number)
    mxlFabricsProvider provider;           ///< Fabric provider (TCP, Verbs, EFA)
};

/**
 * @brief Signal handler for SIGINT/SIGTERM - triggers graceful shutdown
 */
void signal_handler(int)
{
    g_exit_requested = 1;
}

/**
 * @brief Fabrics initiator (sender) implementation
 *
 * Reads grains from a local MXL flow and transmits them to a remote target
 * via RDMA. Manages connection lifecycle and grain transfer progress.
 */
class AppInitator
{
public:
    /**
     * @brief Construct initiator with configuration
     * @param config Configuration including domain, flow ID, and network parameters
     */
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

    /**
     * @brief Set up the initiator: create flow reader, register memory, connect to target
     *
     * This function:
     *   1. Creates MXL instance and flow reader
     *   2. Creates fabrics initiator
     *   3. Registers flow memory regions for RDMA
     *   4. Parses target info and establishes connection
     *
     * @param targetInfoStr Base64-encoded target information from the receiver
     * @return MXL_STATUS_OK on success, error code otherwise
     */
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
        status = mxlCreateFlowReader(_instance, _config.flowID.c_str(), "", &_reader);
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

        mxlRegions regions;
        status = mxlFabricsRegionsForFlowReader(_reader, &regions);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get flow memory region with status '{}'", static_cast<int>(status));
            return status;
        }

        mxlInitiatorConfig initiatorConfig = {
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
            status = mxlFabricsInitiatorMakeProgressBlocking(_initiator, 250);
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

    /**
     * @brief Main transmission loop: read grains locally and transfer via RDMA
     *
     * This function continuously:
     *   1. Waits for the next grain to be available locally
     *   2. Transfers it to the remote target via mxlFabricsInitiatorTransferGrain
     *   3. Polls for transfer completion
     *   4. Handles errors (too early, too late, etc.)
     *
     * Runs until g_exit_requested is set or a fatal error occurs.
     *
     * @return MXL_STATUS_OK on graceful shutdown, error code on failure
     */
    mxlStatus run()
    {
        // Extract the FlowInfo structure to get grain rate for indexing
        mxlFlowConfigInfo configInfo;
        auto status = mxlFlowReaderGetConfigInfo(_reader, &configInfo);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get flow info with status '{}'", static_cast<int>(status));
            return status;
        }

        mxlGrainInfo grainInfo;
        uint8_t* payload;

        uint64_t grainIndex = mxlGetCurrentIndex(&configInfo.common.grainRate);

        while (!g_exit_requested)
        {
            auto ret = mxlFlowReaderGetGrain(_reader, grainIndex, 200000000, &grainInfo, &payload);
            if (ret == MXL_ERR_OUT_OF_RANGE_TOO_LATE)
            {
                // We are too late.. time travel!
                grainIndex = mxlGetCurrentIndex(&configInfo.common.grainRate);
                continue;
            }
            if (ret == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
            {
                // We are too early somehow.. retry the same grain later.
                continue;
            }
            if (ret != MXL_STATUS_OK)
            {
                // Something  unexpected occured, not much we can do, but log and retry
                MXL_ERROR("Missed grain {}, err : {}", grainIndex, (int)ret);

                continue;
            }

            // Okay the grain is ready, we can transfer it to the targets.
            ret = mxlFabricsInitiatorTransferGrain(_initiator, grainIndex);
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
                status = mxlFabricsInitiatorMakeProgressBlocking(_initiator, 10);
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

            if (grainInfo.validSlices != grainInfo.totalSlices)
            {
                // partial commit, we will need to work on the same grain again.
                continue;
            }

            // If we get here, we have transfered the grain completely, we can work on the next grain.
            grainIndex++;
        }

        status = mxlFabricsInitiatorRemoveTarget(_initiator, _targetInfo);
        if (status != MXL_STATUS_OK)
        {
            return status;
        }

        do
        {
            status = mxlFabricsInitiatorMakeProgressBlocking(_initiator, 250);
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
    Config _config;

    mxlInstance _instance;
    mxlFabricsInstance _fabricsInstance;
    mxlFlowReader _reader;
    mxlFabricsInitiator _initiator;
    mxlTargetInfo _targetInfo;
};

/**
 * @brief Fabrics target (receiver) implementation
 *
 * Creates a local MXL flow writer and listens for incoming RDMA connections.
 * Receives grains from remote initiators and commits them to the local flow.
 */
class AppTarget
{
public:
    /**
     * @brief Construct target with configuration
     * @param config Configuration including domain, flow descriptor, and network parameters
     */
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

    /**
     * @brief Set up the target: create flow writer, register memory, start listening
     *
     * This function:
     *   1. Creates MXL instance and flow writer from NMOS descriptor
     *   2. Creates fabrics target
     *   3. Registers flow memory regions for RDMA
     *   4. Sets up listening endpoint
     *   5. Generates target info for initiators to connect
     *
     * @param flowDescriptor NMOS JSON flow definition
     * @return MXL_STATUS_OK on success, error code otherwise
     */
    mxlStatus setup(std::string const& flowDescriptor)
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
        // Create a flow writer for the given flow id.
        status = mxlCreateFlowWriter(_instance, flowDescriptor.c_str(), "", &_writer, &configInfo, &flowCreated);
        if (status != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create flow writer with status '{}'", static_cast<int>(status));
            return status;
        }
        if (!flowCreated)
        {
            MXL_WARN("Reusing existing flow");
        }

        mxlRegions memoryRegions;
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

        mxlTargetConfig targetConfig = {
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

    /**
     * @brief Main reception loop: wait for incoming grains and commit to local flow
     *
     * This function continuously:
     *   1. Waits for a grain to be written via RDMA by an initiator
     *   2. Opens the grain locally (without modifying payload)
     *   3. Commits the grain to make it available to local readers
     *   4. Logs grain details
     *
     * Note: The grain payload and metadata are already written by the remote initiator.
     * The target just needs to open and commit to update the flow head index.
     *
     * Runs until g_exit_requested is set or a fatal error occurs.
     *
     * @return MXL_STATUS_OK on graceful shutdown, error code on failure
     */
    mxlStatus run()
    {
        mxlGrainInfo dummyGrainInfo;
        uint64_t grainIndex = 0;
        uint8_t* dummyPayload;
        mxlStatus status;

        while (!g_exit_requested)
        {
            // Wait for remote initiator to complete writing a grain
            status = mxlFabricsTargetWaitForNewGrain(_target, &grainIndex, 200);
            if (status == MXL_ERR_TIMEOUT)
            {
                // No completion before a timeout was triggered, most likely a problem upstream
                MXL_WARN("wait for new grain timeout, most likely there is a problem upstream.");
                continue;
            }

            if (status == MXL_ERR_INTERRUPTED)
            {
                return MXL_STATUS_OK;
            }

            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to wait for grain with status '{}'", static_cast<int>(status));
                return status;
            }

            // Here we open so that we can commit, we are not going to modify the grain as it was already modified by the initiator.
            status = mxlFlowWriterOpenGrain(_writer, grainIndex, &dummyGrainInfo, &dummyPayload);
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to open grain with status '{}'", static_cast<int>(status));
                return status;
            }

            // GrainInfo and media payload was already written by the remote endpoint, we simply commit!.
            status = mxlFlowWriterCommitGrain(_writer, &dummyGrainInfo);
            if (status != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to commit grain with status '{}'", static_cast<int>(status));
                return status;
            }

            MXL_INFO("Comitted grain with index={} validSlices={} totalSlices={}, grainSize={}",
                grainIndex,
                dummyGrainInfo.validSlices,
                dummyGrainInfo.totalSlices,
                dummyGrainInfo.grainSize);
        }

        return MXL_STATUS_OK;
    }

private:
    Config _config;

    mxlInstance _instance;
    mxlFabricsInstance _fabricsInstance;
    mxlFlowWriter _writer;
    mxlFabricsTarget _target;
    mxlTargetInfo _targetInfo;
};

/**
 * @brief Main entry point for mxl-fabrics-demo
 *
 * Parses command-line arguments and runs in either:
 *   - Initiator mode (-i): Reads from local flow, transmits to remote target
 *   - Target mode (default): Creates flow writer, receives from remote initiator
 *
 * The workflow is:
 *   1. Parse command-line arguments (domain, flow, network params, mode)
 *   2. If target mode:
 *      - Create flow writer from NMOS JSON
 *      - Setup fabrics target endpoint
 *      - Print target info (base64) for initiator to use
 *      - Run reception loop
 *   3. If initiator mode:
 *      - Create flow reader for existing flow
 *      - Parse target info from command line
 *      - Setup fabrics initiator and connect to target
 *      - Run transmission loop
 *
 * @param argc Argument count
 * @param argv Argument values
 * @return MXL_STATUS_OK on success, error code on failure
 */
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

    bool runAsInitiator;
    auto runAsInitiatorOpt = app.add_flag("-i,--initiator",
        runAsInitiator,
        "Run as an initiator (flow reader + fabrics initiator). If not set, run as a receiver (fabrics target + flow writer).");
    runAsInitiatorOpt->default_val(false);

    std::optional<std::string> node;
    auto nodeOpt = app.add_option("-n,--node",
        node,
        "This corresponds to the interface identifier of the fabrics endpoint, it can also be a logical address. This can be seen as the bind "
        "address when using sockets.");
    nodeOpt->default_val(std::nullopt);

    std::optional<std::string> service;
    auto serviceOpt = app.add_option("--service",
        service,
        "This corresponds to a service identifier for the fabrics endpoint. This can be seen as the bind port when using sockets.");
    serviceOpt->default_val(std::nullopt);

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

        auto app = AppInitator{
            Config{
                   .domain = domain,
                   .flowID = flowConf,
                   .node = node,
                   .service = service,
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

        auto app = AppTarget{
            Config{
                   .domain = domain,
                   .flowID = flowId,
                   .node = node,
                   .service = service,
                   .provider = mxlProvider,
                   },
        };

        if (status = app.setup(flowDescriptor); status != MXL_STATUS_OK)
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
