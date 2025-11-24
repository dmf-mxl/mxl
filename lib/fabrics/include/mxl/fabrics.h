// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

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
#include "mxl/flowinfo.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** Central instance object, holds resources that are global to all initiators and targets.
     */
    typedef struct mxlFabricsInstance_t* mxlFabricsInstance;

    /** The target is the logical receiver of grains transferred over the network. It is the receiver
     *  of transfer requests by the 'initiator'.
     */
    typedef struct mxlFabricsTarget_t* mxlFabricsTarget;

    /** The TargetInfo object holds the local fabric address, keys and memory region addresses for a target. It is returned after setting
     *  up a new target and must be passed to the initiator to connect it.
     */
    typedef struct mxlTargetInfo_t* mxlTargetInfo;

    /** The initiator is the logical sender of grains over the network. It is the initiator of transfer requests
     *  to registered memory regions of a target.
     */
    typedef struct mxlFabricsInitiator_t* mxlFabricsInitiator;

    /** A collection of memory regions that can be the target or the source of remote write operations.
     * Can be obtained by using a flow reader or writer, and converting it to a regions collection
     * with mxlFabricsRegionsForFlowReader() or mxlFabricsRegionsForFlowWriter().
     */
    typedef struct mxlRegions_t* mxlRegions;

    typedef enum mxlFabricsProvider
    {
        MXL_SHARING_PROVIDER_AUTO = 0,  /**< Auto select the best provider. ** This might not be supported by all implementations. */
        MXL_SHARING_PROVIDER_TCP = 1,   /**< Provider that uses linux tcp sockets. */
        MXL_SHARING_PROVIDER_VERBS = 2, /**< Provider for userspace verbs (libibverbs) and librdmcm for connection management. */
        MXL_SHARING_PROVIDER_EFA = 3,   /**< Provider for AWS Elastic Fabric Adapter. */
        MXL_SHARING_PROVIDER_SHM = 4,   /**< Provider used for moving data between 2 memory regions inside the same system. Supported */
    } mxlFabricsProvider;

    /** Address of a logical network endpoint. This is analogous to a hostname and port number in classic ipv4 networking.
     * The actual values for node and service vary between providers, but often an ip address as the node value and a port number as the service
     * value are sufficient.
     * `node` and `service` pointers are expected to live at least until the target or initiator `setup` function is executed and are
     * internally cloned.
     */
    typedef struct mxlEndpointAddress_t
    {
        char const* node;
        char const* service;
    } mxlEndpointAddress;

    /** Configuration object required to set up a new target.
     */
    typedef struct mxlTargetConfig_t
    {
        mxlEndpointAddress endpointAddress; /**< Bind address for the local endpoint. */
        mxlFabricsProvider provider;        /**< The provider that should be used */
        mxlRegions regions;                 /**< Local memory regions of the flow that grains should be written to. */
        bool deviceSupport;                 /**< Require support of transfers involving device memory. */
    } mxlTargetConfig;

    /** Configuration object required to set up an initiator.
     */
    typedef struct mxlInitiatorConfig_t
    {
        mxlEndpointAddress endpointAddress; /**< Bind address for the local endpoint. */
        mxlFabricsProvider provider;        /**< The provider that should be used. */
        mxlRegions regions;                 /**< Local memory regions of the flow that grains should source of remote write requests. */
        bool deviceSupport;                 /**< Require support of transfers involving device memory. */
    } mxlInitiatorConfig;

    /** Configuration for a memory region location.
     */
    typedef struct mxlFabricsMemoryRegionLocation_t
    {
        mxlPayloadLocation type; /**< Memory type of the payload. */
        uint64_t deviceId;       /**< Device Index when device memory is used, otherwise it is ignored. */
    } mxlFabricsMemoryRegionLocation;

    /** Configuration for a user supplied memory region.
     */
    typedef struct mxlFabricsMemoryRegion_t
    {
        uintptr_t addr;                     /**< Start address of the contiguous memory region. */
        size_t size;                        /**< Size of that memory region */
        mxlFabricsMemoryRegionLocation loc; /**< Location information for that memory region. */
    } mxlFabricsMemoryRegion;

    /** User configuration for a collection of memory regions.
     */
    typedef struct mxlFabricsUserRegionsConfig_t
    {
        mxlFabricsMemoryRegion const* regions;        /**< Pointer to an array of memory regions. */
        size_t regionsCount;                          /**< The number of memory regions in the array. */

        uint32_t sliceSize[MXL_MAX_PLANES_PER_GRAIN]; /**< The size of a single slice in bytes. */
    } mxlFabricsUserRegionsConfig;

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
     * \param in_config User configuiration for the memory regions.
     * \param out_regions Returns a pointer to the created regions object. The user is responsible for freeing this object by calling
     * `mxlFabricsRegionsFree()`.
     * \return MXL_STATUS_OK if the regions object was successfully created.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsRegionsFromUserBuffers(mxlFabricsUserRegionsConfig const* in_config, mxlRegions* out_regions);

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
     * \param out_entryIndex The index of the grain ring buffer index that is ready, if any.
     * \param out_sliceIndex The last valid slice index that is ready, if any.
     * \return The result code. MXL_ERR_NOT_READY if no grain was available at the time of the call, and the call should be retried. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetTryNewGrain(mxlFabricsTarget in_target, uint16_t* out_entryIndex, uint16_t* out_sliceIndex);

    /**
     * Blocking accessor for a flow grain at a specific index.
     * \param in_target A valid fabrics target
     * \param out_entryIndex The index of the grain ring buffer index that is ready, if any.
     * \param out_sliceIndex The last valid slice index that is ready, if any.
     * \param in_timeoutMs How long should we wait for the grain (in milliseconds)
     * \return The result code. MXL_ERR_NOT_READY if no grain was available before the timeout. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetWaitForNewGrain(mxlFabricsTarget in_target, uint16_t* out_entryIndex, uint16_t* out_sliceIndex, uint16_t in_timeoutMs);

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
     * Enqueue a transfer operation to a specific target. This function is always non-blocking. The transfer operation might be started right
     * away, but is only guaranteed to have completed after mxlFabricsInitiatorMakeProgress*() no longer returns MXL_ERR_NOT_READY.
     * \param in_initiator A valid fabrics initiator
     * \param in_targetInfo The target information of the specific target. This should be the same as the one returned from "mxlFabricsTargetSetup".
     * \param in_localIndex The index of the memory region (local) to transfer. The ordering was given when mxlRegions object was created.
     * \param in_remoteIndex The index of the memory region (remote) to receive the transfer at the target side. The ordering was given when
     * mxlRegions object was created.
     * \param in_payloadOffset Offset in bytes inside the remote memory region before the payload starts. This is typically the header if any. In MXL,
     * this corresponds to MXL_GRAIN_PAYLOAD_OFFSET.
     * \param in_startSlice The start slice in the slice range to transfer. This is inclusive.
     * \param in_endSlice The end slice in the slice range to transfer. This is exclusive.
     * \return The result code. \see mxlStatus
     * \note This function is useful when the underlying buffer layout does not match the MXL grain data layout. Otherwise \see
     * mxlFabricsInitiatorTransferGrain()
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorTransferGrainToTarget(mxlFabricsInitiator in_initiator, mxlTargetInfo const in_targetInfo, uint64_t in_localIndex,
        uint64_t in_remoteIndex, uint64_t in_payloadOffset, uint16_t in_startSlice, uint16_t in_endSlice);

    /**
     * Enqueue a transfer operation to all added targets. This function is always non-blocking. The transfer operation might be started right
     * away, but is only guaranteed to have completed after mxlFabricsInitiatorMakeProgress*() no longer returns MXL_ERR_NOT_READY.
     * \param in_initiator A valid fabrics initiator
     * \param in_grainIndex The  grain index to transfer. The ordering was given when mxlRegions object were created. This is true for both local and
     * remote memory regions.
     * \param in_payloadOffset Offset in bytes inside the remote memory region before the payload starts. In MXL, this corresponds to
     * MXL_GRAIN_PAYLOAD_OFFSET.
     * \param in_startSlice The start slice in the slice range to transfer. This is inclusive.
     * \param in_endSlice The end slice in the slice range to transfer. This is exclusive.
     * \return The result code. \see mxlStatus
     * \note This function assumes: (1) the underlying buffer layout matches the MXL grain data layout, and (2) ring buffer entries for local and
     * remote regions can be calculated via modulo operation: `grainIndex % regions.size()`.If these assumptions do not hold, use
     * mxlFabricsInitiatorTransferGrainToTarget() instead.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorTransferGrain(mxlFabricsInitiator in_initiator, uint64_t in_grainIndex, uint64_t in_payloadOffset,
        uint16_t in_startSlice, uint16_t in_endSlice);

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
