// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file fabrics.h
 * @brief MXL Fabrics Public C API - High-performance remote memory access for media exchange
 *
 * This header defines the public C API for MXL's fabrics subsystem, which extends MXL's local
 * shared-memory media exchange to remote hosts via RDMA (Remote Direct Memory Access).
 *
 * ARCHITECTURE OVERVIEW:
 * - MXL core provides zero-copy shared memory exchange between processes on the same machine
 * - The Fabrics layer extends this to network-connected machines using OpenFabrics Interface (OFI/libfabric)
 * - OFI abstracts various RDMA hardware (InfiniBand, RoCE, AWS EFA, etc.) behind a portable API
 *
 * KEY CONCEPTS:
 * - **Target**: The logical receiver of media grains transferred over the network
 * - **Initiator**: The logical sender that pushes media grains to one or more targets
 * - **Regions**: Memory areas registered with the fabric hardware for zero-copy RDMA operations
 * - **Provider**: The underlying transport implementation (TCP, Verbs/InfiniBand, EFA, SHM, etc.)
 *
 * TYPICAL WORKFLOW:
 * 1. Create an mxlFabricsInstance from an mxlInstance
 * 2. On the receiving side:
 *    a. Create a target with mxlFabricsCreateTarget()
 *    b. Configure it with mxlFabricsTargetSetup() - this returns connection info (mxlTargetInfo)
 *    c. Share the mxlTargetInfo (serialized) to the sending side (out-of-band)
 *    d. Poll for incoming grains with mxlFabricsTargetTryNewGrain() or mxlFabricsTargetWaitForNewGrain()
 * 3. On the sending side:
 *    a. Create an initiator with mxlFabricsCreateInitiator()
 *    b. Configure it with mxlFabricsInitiatorSetup()
 *    c. Add target(s) with mxlFabricsInitiatorAddTarget() using the received mxlTargetInfo
 *    d. Transfer grains with mxlFabricsInitiatorTransferGrain()
 *    e. Call mxlFabricsInitiatorMakeProgress*() to pump the network operations
 *
 * MEMORY REGISTRATION:
 * - Before RDMA can occur, memory must be "pinned" and registered with the network hardware
 * - Use mxlFabricsRegionsForFlowReader/Writer() to extract regions from MXL flows
 * - Or use mxlFabricsRegionsFromUserBuffers() for custom memory buffers
 *
 * THREADING:
 * - Each target/initiator operates independently and can be polled from different threads
 * - Users are responsible for synchronization if sharing objects across threads
 */

#pragma once

#ifdef __cplusplus
#   include <cstddef>
#   include <cstdint>
#else
#   include <stddef.h>
#   include <stdint.h>
#endif

#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/platform.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Central fabrics instance object - manages resources shared across targets and initiators
     *
     * This is the root object for the fabrics subsystem. It holds global resources including
     * the underlying libfabric fabric and domain objects. All targets and initiators must be
     * created from an instance.
     *
     * Opaque handle representing an mxlFabricsInstance_t structure.
     */
    typedef struct mxlFabricsInstance_t* mxlFabricsInstance;

    /**
     * @brief Target - the logical receiver of media grains over the network
     *
     * A target represents a receiving endpoint in the fabrics system. It:
     * - Listens for incoming RDMA write operations from initiators
     * - Exposes registered memory regions that initiators can write to
     * - Provides an interface to poll for newly received grains
     *
     * Targets are passive - they do not initiate transfers, they only receive them.
     * Multiple initiators can write to a single target concurrently.
     *
     * Opaque handle representing an mxlFabricsTarget_t structure.
     */
    typedef struct mxlFabricsTarget_t* mxlFabricsTarget;

    /**
     * @brief Connection information for a target - shared with initiators to enable remote access
     *
     * TargetInfo is an opaque data structure containing everything an initiator needs to connect
     * to a target and perform RDMA writes:
     * - The target's fabric network address (analogous to IP:port)
     * - Memory region keys (RKEY) for accessing the target's registered buffers
     * - Remote virtual addresses of the target's memory regions
     *
     * This object is returned by mxlFabricsTargetSetup() and must be serialized using
     * mxlFabricsTargetInfoToString() to be transmitted to remote initiators (via out-of-band
     * communication such as config files, REST API, signaling channel, etc.).
     *
     * Opaque handle representing an mxlTargetInfo_t structure.
     */
    typedef struct mxlTargetInfo_t* mxlTargetInfo;

    /**
     * @brief Initiator - the logical sender of media grains over the network
     *
     * An initiator represents a sending endpoint in the fabrics system. It:
     * - Initiates RDMA write operations to push media grains to targets
     * - Can connect to multiple targets and multicast the same grain to all of them
     * - Manages a work queue of pending transfer operations
     *
     * Initiators are active - they drive the data movement. The user enqueues transfers
     * via mxlFabricsInitiatorTransferGrain() and then must regularly call
     * mxlFabricsInitiatorMakeProgress*() to pump the network operations.
     *
     * Opaque handle representing an mxlFabricsInitiator_t structure.
     */
    typedef struct mxlFabricsInitiator_t* mxlFabricsInitiator;

    /**
     * @brief A collection of memory regions for RDMA operations
     *
     * Regions represent one or more contiguous memory areas that have been registered
     * (or will be registered) with the fabrics layer for zero-copy RDMA access.
     *
     * In MXL, regions typically correspond to the backing memory buffers of a Flow
     * (obtained via mxlFabricsRegionsForFlowReader/Writer), but can also represent
     * arbitrary user-provided buffers (via mxlFabricsRegionsFromUserBuffers).
     *
     * The same memory can be used as both a source (on an initiator) and a destination
     * (on a target), but the registration and access permissions differ.
     *
     * Opaque handle representing an mxlRegions_t structure.
     */
    typedef struct mxlRegions_t* mxlRegions;

    /**
     * @brief Fabric provider selection - specifies the underlying transport mechanism
     *
     * OFI/libfabric supports multiple "providers" - different implementations of the RDMA API
     * that run over various network fabrics. Each provider has different characteristics in terms
     * of performance, latency, compatibility, and hardware requirements.
     *
     * PROVIDER DETAILS:
     * - **AUTO**: Let libfabric choose the best available provider for your system (may not work in all cases)
     * - **TCP**: Software-based using standard TCP sockets - works on any network, lowest performance
     * - **VERBS**: Hardware RDMA using InfiniBand or RoCE (RDMA over Converged Ethernet) via libibverbs
     * - **EFA**: AWS Elastic Fabric Adapter - custom RDMA protocol for EC2 instances with EFA-enabled NICs
     * - **SHM**: Shared memory provider for intra-node transfers (same machine)
     *
     * CHOOSING A PROVIDER:
     * - Use VERBS for on-premise InfiniBand or RoCE deployments
     * - Use EFA for AWS EC2 with EFA-enabled instance types (c5n, p3dn, etc.)
     * - Use TCP for testing or when no RDMA hardware is available
     * - Use SHM for testing multi-process scenarios on a single machine
     */
    typedef enum mxlFabricsProvider
    {
        MXL_SHARING_PROVIDER_AUTO = 0,  /**< Auto-select the best provider (may not be supported by all implementations) */
        MXL_SHARING_PROVIDER_TCP = 1,   /**< Software transport using standard TCP sockets - universally compatible but slowest */
        MXL_SHARING_PROVIDER_VERBS = 2, /**< Hardware RDMA using InfiniBand or RoCE via libibverbs userspace library */
        MXL_SHARING_PROVIDER_EFA = 3,   /**< AWS Elastic Fabric Adapter - custom RDMA for AWS EC2 (requires EFA-enabled instances) */
        MXL_SHARING_PROVIDER_SHM = 4,   /**< Shared memory provider for intra-node (same-machine) data movement */
    } mxlFabricsProvider;

    /**
     * @brief Network endpoint address - identifies where a target or initiator binds/listens
     *
     * This structure is analogous to a (hostname, port) pair in traditional TCP/IP networking,
     * but the interpretation of 'node' and 'service' varies by provider.
     *
     * PROVIDER-SPECIFIC INTERPRETATIONS:
     * - **TCP/VERBS**: node = IP address (e.g. "192.168.1.100" or NULL for INADDR_ANY),
     *                  service = port number as string (e.g. "5000")
     * - **EFA**: node = IP address, service = port number (EFA uses UDP underneath)
     * - **SHM**: node/service typically ignored or used to generate unique shared memory names
     *
     * MEMORY LIFETIME:
     * The `node` and `service` pointers must remain valid until the corresponding
     * mxlFabricsTargetSetup() or mxlFabricsInitiatorSetup() call completes. The implementation
     * will internally clone these strings, so the caller can free them afterward.
     *
     * NULL VALUES:
     * - node=NULL typically means "bind to all interfaces" or "use default"
     * - service=NULL may allow the system to assign an ephemeral port (provider-dependent)
     */
    typedef struct mxlEndpointAddress_t
    {
        char const* node;    /**< Network node identifier (e.g. IP address or hostname) - provider-specific interpretation */
        char const* service; /**< Service identifier (e.g. port number) - provider-specific interpretation */
    } mxlEndpointAddress;

    /**
     * @brief Configuration for setting up a target (receiver)
     *
     * This structure specifies all parameters needed to initialize a target endpoint that will
     * receive media grains via RDMA writes.
     */
    typedef struct mxlTargetConfig_t
    {
        mxlEndpointAddress endpointAddress; /**< Bind address for the local endpoint (where to listen for incoming connections/writes) */
        mxlFabricsProvider provider;        /**< Which fabric provider to use (TCP, VERBS, EFA, SHM) */
        mxlRegions regions;                 /**< Local memory regions where incoming grains will be written (destination buffers) */
        bool deviceSupport;                 /**< If true, require provider support for GPU/device memory (e.g. CUDA, ROCm) - not all providers support this */
    } mxlTargetConfig;

    /**
     * @brief Configuration for setting up an initiator (sender)
     *
     * This structure specifies all parameters needed to initialize an initiator endpoint that will
     * send media grains to remote targets via RDMA writes.
     */
    typedef struct mxlInitiatorConfig_t
    {
        mxlEndpointAddress endpointAddress; /**< Bind address for the local endpoint (source address for outgoing connections/writes) */
        mxlFabricsProvider provider;        /**< Which fabric provider to use (must match the target's provider in most cases) */
        mxlRegions regions;                 /**< Local memory regions containing the grains to send (source buffers) */
        bool deviceSupport;                 /**< If true, require provider support for GPU/device memory (e.g. CUDA, ROCm) - not all providers support this */
    } mxlInitiatorConfig;

    /**
     * @brief Memory region location descriptor - specifies where memory physically resides
     *
     * RDMA hardware needs to know whether memory is in system RAM or on a GPU/accelerator device,
     * as different code paths and DMA engines are used for each.
     */
    typedef struct mxlFabricsMemoryRegionLocation_t
    {
        mxlPayloadLocation type; /**< Memory type: host RAM (MXL_LOCATION_HOST) or device memory (MXL_LOCATION_DEVICE) */
        uint64_t deviceId;       /**< GPU/device index (e.g. CUDA device 0, 1, etc.) - only used if type is MXL_LOCATION_DEVICE, ignored otherwise */
    } mxlFabricsMemoryRegionLocation;

    /**
     * @brief User-supplied memory region descriptor
     *
     * When using custom buffers (not MXL Flows), this structure describes a single contiguous
     * memory region that should be registered with the fabrics layer for RDMA operations.
     *
     * REQUIREMENTS:
     * - Memory must be contiguous in virtual address space
     * - Memory should ideally be page-aligned for best performance
     * - Memory must remain valid and pinned (not paged out) for the duration of RDMA operations
     */
    typedef struct mxlFabricsMemoryRegion_t
    {
        uintptr_t addr;                     /**< Start address of the contiguous memory region (virtual address) */
        size_t size;                        /**< Size in bytes of the memory region */
        mxlFabricsMemoryRegionLocation loc; /**< Location descriptor indicating if this is host or device memory */
    } mxlFabricsMemoryRegion;

    /**
     * Get the backing memory regions of a flow associated with a flow reader.
     * The regions will be used to register the shared memory of the reader as source of data transfer operations.
     * The returned object must be freed with mxlFabricsRegionsFree(). The object can be freed after the target or initiator has been created.
     * \param in_reader FlowReader to use to obtain these regions.
     * \param out_regions A pointer to a memory location where the address of the returned collection of memory regions will be written.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsRegionsForFlowReader(mxlFlowReader in_reader, mxlRegions* out_regions);

    /**
     * Get the backing memory regions of a flow associated with a flow writer.
     * The regions will be used to register the shared memory of the writer as the target of data transfer operations.
     * The returned object must be freed with mxlFabricsRegionsFree(). The object can be freed after the target or initiator has been created.
     * \param in_writer FlowWriter to use to obtain these regions.
     * \param out_regions A pointer to a memory location where the address of the returned collection of memory regions will be written.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsRegionsForFlowWriter(mxlFlowWriter in_writer, mxlRegions* out_regions);

    /**
     * Create a regions object from a list of memory region groups.
     * \param in_regions A pointer to an array of memory region groups.
     * \param in_count The number of memory region groups in the array.
     * \param out_regions Returns a pointer to the created regions object. The user is responsible for freeing this object by calling
     * `mxlFabricsRegionsFree()`.
     * \return MXL_STATUS_OK if the regions object was successfully created.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsRegionsFromUserBuffers(mxlFabricsMemoryRegion const* in_regions, size_t in_count, mxlRegions* out_regions);

    /**
     * Free a regions object previously allocated by mxlFabricsRegionsForFlowReader(), mxlFabricsRegionsForFlowWriter() or
     * mxlFabricsRegionsFromUserBuffers().
     * \param in_regions The regions object to free
     * \return MXL_STATUS_OK if the regions object was freed
     */
    MXL_EXPORT
    mxlStatus mxlFabricsRegionsFree(mxlRegions in_regions);

    /**
     * Create a new mxl-fabrics from an mxl instance. Targets and initiators created from this mxl-fabrics instance
     * will have access to the flows created in the supplied mxl instance.
     * \param in_instance An mxlInstance previously created with mxlCreateInstance().
     * \param out_fabricsInstance Returns a pointer to the created mxlFabricsInstance.
     * \return MXL_STATUS_OK if the instance was successfully created
     */
    MXL_EXPORT
    mxlStatus mxlFabricsCreateInstance(mxlInstance in_instance, mxlFabricsInstance* out_fabricsInstance);

    /**
     * Destroy a mxlFabricsInstance.
     * \param in_instance The mxlFabricsInstance to destroy.
     * \return MXL_STATUS_OK if the instances was successfully destroyed.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsDestroyInstance(mxlFabricsInstance in_instance);

    /**
     * Create a fabrics target. The target is the receiver of write operations from an initiator.
     * \param in_fabricsInstance A valid mxl fabrics instance
     * \param out_target A valid fabrics target
     */
    MXL_EXPORT
    mxlStatus mxlFabricsCreateTarget(mxlFabricsInstance in_fabricsInstance, mxlFabricsTarget* out_target);

    /**
     * Destroy a fabrics target instance.
     * \param in_fabricsInstance A valid mxl fabrics instance
     * \param in_target A valid fabrics target
     */
    MXL_EXPORT
    mxlStatus mxlFabricsDestroyTarget(mxlFabricsInstance in_fabricsInstance, mxlFabricsTarget in_target);

    /**
     * Configure the target. After the target has been configured, it is ready to receive transfers from an initiator.
     * If additional connection setup is required by the underlying implementation it might not happen during the call to
     * mxlFabricsTargetSetup, but be deferred until the first call to mxlFabricsTargetTryNewGrain().
     * \param in_target A valid fabrics target
     * \param in_config The target configuration. This will be used to create an endpoint and register a memory region. The memory region
     * corresponds to the one that will be written to by the initiator.
     * \param out_info An mxlTargetInfo_t object which should be shared to a remote initiator which this target should receive data from. The
     * object must be freed with mxlFabricsFreeTargetInfo().
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetSetup(mxlFabricsTarget in_target, mxlTargetConfig* in_config, mxlTargetInfo* out_info);

    /**
     * Non-blocking accessor for a flow grain at a specific index.
     * \param in_target A valid fabrics target
     * \param out_index The index of the grain that is ready, if any.
     * \return The result code. MXL_ERR_NOT_READY if no grain was available at the time of the call, and the call should be retried. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetTryNewGrain(mxlFabricsTarget in_target, uint64_t* out_index);

    /**
     * Blocking accessor for a flow grain at a specific index.
     * \param in_target A valid fabrics target
     * \param out_index The index of the grain that is ready, if any.
     * \param in_timeoutMs How long should we wait for the grain (in milliseconds)
     * \return The result code. MXL_ERR_NOT_READY if no grain was available before the timeout. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetWaitForNewGrain(mxlFabricsTarget in_target, uint64_t* out_index, uint16_t in_timeoutMs);

    /**
     * Create a fabrics initiator instance.
     * \param in_fabricsInstance A valid mxl fabrics instance
     * \param out_initiator A valid fabrics initiator
     */
    MXL_EXPORT
    mxlStatus mxlFabricsCreateInitiator(mxlFabricsInstance in_fabricsInstance, mxlFabricsInitiator* out_initiator);

    /**
     * Destroy a fabrics initiator instance.
     * \param in_fabricsInstance A valid mxl fabrics instance
     * \param in_initiator A valid fabrics initiator
     */
    MXL_EXPORT
    mxlStatus mxlFabricsDestroyInitiator(mxlFabricsInstance in_fabricsInstance, mxlFabricsInitiator in_initiator);

    /**
     * Configure the initiator.
     * \param in_initiator A valid fabrics initiator
     * \param in_config The initiator configuration. This will be used to create an endpoint and register a memory region. The memory region
     * corresponds to the one that will be shared with targets.
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorSetup(mxlFabricsInitiator in_initiator, mxlInitiatorConfig const* in_config);

    /**
     * Add a target to the initiator. This will allow the initiator to send data to the target in subsequent calls to
     * mxlFabricsInitiatorTransferGrain(). This function is always non-blocking. If additional connection setup is required by the underlying
     * implementation, it will only happen during a call to mxlFabricsInitiatorMakeProgress*().
     * \param in_initiator A valid fabrics initiator
     * \param in_targetInfo The target information. This should be the same as the one returned from "mxlFabricsTargetSetup".
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorAddTarget(mxlFabricsInitiator in_initiator, mxlTargetInfo const in_targetInfo);

    /**
     * Remove a target from the initiator. This function is always non-blocking. If any additional communication for a graceful shutdown is
     * required it will happend during a call to mxlFabricsInitiatorMakeProgress*(). It is guaranteed that no new grain transfer operations will
     * be queued for this target during calls to mxlFabricsInitiatorTransferGrain() after the target was removed, but it is only guaranteed that
     * the connection shutdown has completed after mxlFabricsInitiatorMakeProgress*() no longer returns MXL_ERR_NOT_READY.
     * \param in_initiator A valid fabrics initiator
     * \param in_targetInfo The target information. This should be the same as the one returned from "mxlFabricsTargetSetup".
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorRemoveTarget(mxlFabricsInitiator in_initiator, mxlTargetInfo const in_targetInfo);

    /**
     * Enqueue a transfer operation to all added targets. This function is always non-blocking. The transfer operation might be started right
     * away, but is only guaranteed to have completed after mxlFabricsInitiatorMakeProgress*() no longer returns MXL_ERR_NOT_READY.
     * \param in_initiator A valid fabrics initiator
     * \param in_grainIndex The index of the grain to transfer.
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorTransferGrain(mxlFabricsInitiator in_initiator, uint64_t in_grainIndex);

    /**
     * This function must be called regularly for the initiator to make progress on queued transfer operations, connection establishment
     * operations and connection shutdown operations.
     * \param in_initiator The initiator that should make progress.
     * \return The result code. Returns MXL_ERR_NOT_READY if there is still progress to be made, and not all operations have completed.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorMakeProgressNonBlocking(mxlFabricsInitiator in_initiator);

    /**
     * This function must be called regularly for the initiator to make progress on queued transfer operations, connection establishment
     * operations and connection shutdown operations.
     * \param in_initiator The initiator that should make progress.
     * \param in_timeoutMs The maximum time to wait for progress to be made (in milliseconds).
     * \return The result code. Returns MXL_ERR_NOT_READY if there is still progress to be made and not all operations have completed before the
     * timeout.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorMakeProgressBlocking(mxlFabricsInitiator in_initiator, uint16_t in_timeoutMs);

    // Below are helper functions

    /**
     * Convert a string to a fabrics provider enum value.
     * \param in_string A valid string to convert
     * \param out_provider A valid fabrics provider to convert to
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsProviderFromString(char const* in_string, mxlFabricsProvider* out_provider);

    /**
     * Convert a fabrics provider enum value to a string.
     * \param in_provider A valid fabrics provider to convert
     * \param out_string A user supplied buffer of the correct size. Initially you can pass a NULL pointer to obtain the size of the string.
     * \param in_stringSize The size of the output string.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsProviderToString(mxlFabricsProvider in_provider, char* out_string, size_t* in_stringSize);

    /**
     * Serialize a target info object obtained from mxlFabricsTargetSetup() into a string representation.
     * \param in_targetInfo A valid target info to serialize
     * \param out_string A user supplied buffer of the correct size. Initially you can pass a NULL pointer to obtain the size of the string.
     * \param in_stringSize The size of the output string.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetInfoToString(mxlTargetInfo const in_targetInfo, char* out_string, size_t* in_stringSize);

    /**
     * Parse a targetInfo object from its string representation.
     * \param in_string A valid string to deserialize
     * \param out_targetInfo A valid target info to deserialize to
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetInfoFromString(char const* in_string, mxlTargetInfo* out_targetInfo);

    /**
     * Free a mxlTargetInfo object obtained from mxlFabricsTargetSetup() or mxlFabricsTargetInfoFromString().
     * \param in_info A mxlTargetInfo object
     * \return MXL_STATUS_OK if the mxlTargetInfo object was freed.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsFreeTargetInfo(mxlTargetInfo in_info);

#ifdef __cplusplus
}
#endif
