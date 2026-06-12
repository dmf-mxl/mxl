// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
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

#define MXL_FABRICS_API_VERSION 0 /**< Version of the API defined by this header. */
#define MXL_FABRICS_FLAG(n)     (1 << n)

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
    typedef struct mxlFabricsTargetInfo_t* mxlFabricsTargetInfo;

    /** The initiator is the logical sender of grains over the network. It is the initiator of transfer requests
     *  to registered memory regions of a target.
     */
    typedef struct mxlFabricsInitiator_t* mxlFabricsInitiator;

    typedef enum mxlFabricsProvider_t
    {
        MXL_FABRICS_PROVIDER_ANY = 0,   /**< Auto select the best provider. ** This might not be supported by all implementations. Currently this
                                           allways falls back to the TCP provider. */
        MXL_FABRICS_PROVIDER_TCP = 1,   /**< Provider that uses linux tcp sockets. */
        MXL_FABRICS_PROVIDER_VERBS = 2, /**< Provider for userspace verbs (libibverbs) and librdmcm for connection management. */
        MXL_FABRICS_PROVIDER_EFA = 3,   /**< Provider for AWS Elastic Fabric Adapter. */
        MXL_FABRICS_PROVIDER_SHM = 4,   /**< Provider used for moving data between 2 memory regions inside the same system. Supported */
    } mxlFabricsProvider;

    /** Address of a logical network endpoint. This is analogous to a hostname and port number in classic ipv4 networking.
     * The actual values for node and service vary between providers, but often an ip address as the node value and a port number as the service
     * value are sufficient.
     * `node` and `service` pointers are expected to live at least until the target or initiator `setup` function is executed and are
     * internally cloned.
     */
    typedef struct mxlFabricsEndpointAddress_t
    {
        char const* node;
        char const* service;
    } mxlFabricsEndpointAddress;

    /** Capability flags describing what an interface supports.
     *
     * When returned by mxlFabricsGetInterfaces(), these flags are informational: they describe the capabilities
     * of each discovered interface. The caller reads them to decide which interface to use.
     *
     * When passed to mxlFabricsTargetSetup() or mxlFabricsInitiatorSetup() (via mxlFabricsInterfaceConfig), these
     * flags express requirements. At least one transfer capability (REMOTE_WRITE or SEND_RECEIVE) must be set.
     * If no transfer capability is set, REMOTE_WRITE is assumed.
     */
    typedef enum mxlFabricsInterfaceCapFlags_t
    {
        MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS = MXL_FABRICS_FLAG(0), /**< The interface supports blocking operations. */
        MXL_FABRICS_IFACE_CAP_REMOTE_WRITE = MXL_FABRICS_FLAG(1),        /**< The interface supports remote-write (RDMA) operations. */
        MXL_FABRICS_IFACE_CAP_SEND_RECEIVE = MXL_FABRICS_FLAG(2),        /**< The interface supports send/receive message operations. */
    } mxlFabricsInterfaceCapFlags;

    /** Capabilities of an interface. Returned as informational output by mxlFabricsGetInterfaces().
     *  When used inside mxlFabricsInterfaceConfig for setup functions, only the \c flags field is
     *  evaluated as requirements; \c maxMessageSize is ignored.
     */
    typedef struct mxlFabricsInterfaceCaps_t
    {
        int version;             /**< Struct version, must be set to MXL_FABRICS_API_VERSION by the caller. */
        uint64_t flags;          /**< Bitwise OR of mxlFabricsInterfaceCapFlags values.
                                      See mxlFabricsInterfaceCapFlags for the dual semantics (informational vs. requirements). */
        uint64_t maxMessageSize; /**< Maximum message size supported on this interface. Reported by mxlFabricsGetInterfaces().
                                      Currently not enforced by setup functions, but callers should initialize this field to a
                                      valid value as it will be required in a future version. */
    } mxlFabricsInterfaceCaps;

    typedef struct mxlFabricsInterfaceConfig_t
    {
        int version;                       /**< Struct version, must be set to MXL_FABRICS_API_VERSION by the caller. */
        mxlFabricsProvider provider;       /**< The provider that the interface can be used with. */
        mxlFabricsInterfaceCaps caps;      /**< Interface capabilities. Output-only for mxlFabricsGetInterfaces(); requirements for setup functions.
                                                See mxlFabricsInterfaceCapFlags for details. */
        mxlFabricsEndpointAddress address; /**< Address (node/service) of this interface. */

        /**
         * Extra information about the interface in a json document. Provided on a best-effort basis. Not all fields are available on certain
         * platforms and hardware.
         * Only provided by a call to mxlFabricsGetInterfaces, and ignored when provided through a setup function.
         */
        char const* attr;
    } mxlFabricsInterfaceConfig;

    /**
     * A list of interfaces returned by mxlFabricsGetInterfaces. Should be freed with mxlFabricsFreeInterfaceList
     */
    typedef struct mxlFabricsInterfaceList_t mxlFabricsInterfaceList;

    /** \copydoc mxlFabricsFreeInterfaceList */
    struct mxlFabricsInterfaceList_t
    {
        mxlFabricsInterfaceList* next;       /**< Next entry in the list. NULL at the last entry. */
        mxlFabricsInterfaceConfig interface; /**< A single interface available on this host. */
    };

    /** Configuration object required to set up a new target.
     */
    typedef struct mxlFabricsTargetConfig_t
    {
        int version;                         /**< Struct version, must be set to MXL_FABRICS_API_VERSION by the caller */
        mxlFabricsInterfaceConfig interface; /**< Bind address for the local endpoint. */
        mxlFlowWriter writer;                /**< A flow writer for the flow that should be written to by remote initiators */
    } mxlFabricsTargetConfig;

    /** Configuration object required to set up an initiator.
     */
    typedef struct mxlFabricsInitiatorConfig_t
    {
        int version;                         /**< Struct version, must be set to MXL_FABRICS_API_VERSION by the caller */
        mxlFabricsInterfaceConfig interface; /**< Bind address for the local endpoint. */
        mxlFlowReader reader;                /**< A flow reader that will be used as the source of write operations to remote targets. */
    } mxlFabricsInitiatorConfig;

    /**
     * Create a new mxl-fabrics from an mxl instance. Targets and initiators created from this mxl-fabrics instance
     * will have access to the flows created in the supplied mxl instance.
     * \param in_instance An mxlInstance previously created with mxlCreateInstance().
     * \param options A json-formatted string with options. Currently not used.
     * \param out_fabricsInstance Returns a pointer to the created mxlFabricsInstance.
     * \return MXL_STATUS_OK if the instance was successfully created
     */
    MXL_EXPORT
    mxlStatus mxlFabricsCreateInstance(mxlInstance in_instance, char const* options, mxlFabricsInstance* out_fabricsInstance);

    /**
     * Destroy a mxlFabricsInstance.
     * \param in_instance The mxlFabricsInstance to destroy.
     * \return MXL_STATUS_OK if the instances was successfully destroyed.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsDestroyInstance(mxlFabricsInstance in_instance);

    /**
     * Query a list of available providers, interfaces and their capabilities on this system.
     * \param[in] in_instance A valid mxlFabricsInstance.
     * \param[in] query (Optional) Can be used to filter the items returned by this function. Only the provider and address fields are used for
     * filtering; capability flags in the query are ignored. Use MXL_FABRICS_PROVIDER_ANY or NULL to list interfaces for all providers.
     * \param[out] list A list of interfaces available on the system. Each entry includes the detected capability flags for that interface.
     * The whole list should be freed with mxlFabricsFreeInterfaceList(). This will also free the character arrays returned from this function.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsGetInterfaces(mxlInstance in_instance, mxlFabricsInterfaceConfig const* query, mxlFabricsInterfaceList** list);

    /**
     * Free an interface list previously allocated by mxlFabricsGetInterfaces. This function will also free the character strings in each interface
     * endpoint address struct that is part of the list.
     * \param interfaceList The list of interfaces to free.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsFreeInterfaceList(mxlFabricsInterfaceList* interfaceList);

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
     * \param in_options An optional json-formatted string with options. May be NULL or empty. Recognized fields:
     *  - "cqDepth" (number >= 1): the depth of the target's completion queue. Increase this for high-frame-rate or many-stream
     *    receivers, or when per-completion processing is slow, to avoid completion queue overflow. See the "Receiving grains" section
     *    of docs/Fabrics.md. When omitted, an implementation default is used.
     * \param out_info An mxlFabricsTargetInfo_t object which should be shared to a remote initiator which this target should receive data from. The
     * object must be freed with mxlFabricsFreeTargetInfo().
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetSetup(mxlFabricsTarget in_target, mxlFabricsTargetConfig const* in_config, char const* in_options,
        mxlFabricsTargetInfo* out_info);

    /**
     * Non-blocking accessor for a flow grain.
     * \param in_target A valid fabrics target
     * \param out_grainIndex The index of the grain that was written, if any.
     * \return The result code. MXL_ERR_NOT_READY if no grain was available at the time of the call, and the call should be retried. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetReadGrainNonBlocking(mxlFabricsTarget in_target, uint64_t* out_grainIndex);

    /**
     * Blocking accessor for a flow grain.
     * \param in_target A valid fabrics target
     * \param out_grainIndex The index of the grain that was written, if any.
     * \param in_timeoutMs How long should we wait for the grain (in milliseconds)
     * \return The result code. MXL_ERR_NOT_READY if no grain was available before the timeout. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetReadGrain(mxlFabricsTarget in_target, uint16_t in_timeoutMs, uint64_t* out_entryIndex);

    /**
     * Non-blocking accessor for a flow sample.
     * \param in_target A valid fabrics target
     * \param out_headIndex The head index of the samples that were written, if any.
     * \param out_count The number of samples per channel that were written, if any.
     * \return The result code. MXL_ERR_NOT_READY if no samples were available at the time of the call, and the call should be retried. \see mxlStatus
     * \note One can simply pass the returned headIndex and count to the flow reader's GetSamples() function to obtain a pointer to the written
     * samples after this call returns MXL_STATUS_OK.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetReadSamplesNonBlocking(mxlFabricsTarget in_target, uint64_t* out_headIndex, size_t* out_count);

    /**
     * Blocking accessor for a flow sample.
     * \param in_target A valid fabrics target
     * \param out_headIndex The head index of the samples that were written, if any.
     * \param out_count The number of samples per channel that were written, if any.
     * \param in_timeoutMs How long should we wait for the samples (in milliseconds)
     * \return The result code. MXL_ERR_NOT_READY if no samples were available before the timeout. \see mxlStatus
     * \note One can simply pass the returned headIndex and count to the flow reader's GetSamples() function to obtain a pointer to the written
     * samples after this call returns MXL_STATUS_OK.
     */

    MXL_EXPORT
    mxlStatus mxlFabricsTargetReadSamples(mxlFabricsTarget in_target, uint16_t in_timeoutMs, uint64_t* out_headIndex, size_t* out_count);

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
     * \param in_options A json-formatted string with additional options
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorSetup(mxlFabricsInitiator in_initiator, mxlFabricsInitiatorConfig const* in_config, char const* in_options);

    /**
     * Add a target to the initiator. This will allow the initiator to send data to the target in subsequent calls to
     * mxlFabricsInitiatorTransferGrain(). This function is always non-blocking. If additional connection setup is required by the underlying
     * implementation, it will only happen during a call to mxlFabricsInitiatorMakeProgress*().
     * \param in_initiator A valid fabrics initiator
     * \param in_targetInfo The target information. This should be the same as the one returned from "mxlFabricsTargetSetup".
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorAddTarget(mxlFabricsInitiator in_initiator, mxlFabricsTargetInfo in_targetInfo);

    /**
     * Remove a target from the initiator. This function is always non-blocking. If any additional communication for a graceful shutdown is
     * required it will happend during a call to mxlFabricsInitiatorMakeProgress*(). It is guaranteed that no new grain transfer operations will
     * be queued for this target during calls to mxlFabricsInitiatorTransferGrain() after the target was removed, but it is only guaranteed that
     * the connection shutdown has completed after mxlFabricsInitiatorMakeProgress*() no longer returns MXL_ERR_NOT_READY.
     * \param in_initiator A valid fabrics initiator
     * \param in_targetInfo The target information. This should be the same as the one returned from "mxlFabricsTargetSetup".
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorRemoveTarget(mxlFabricsInitiator in_initiator, mxlFabricsTargetInfo in_targetInfo);

    /**
     * Enqueue a transfer operation to all added targets. This function is always non-blocking. The transfer operation might be started right
     * away, but is only guaranteed to have completed after mxlFabricsInitiatorMakeProgress*() no longer returns MXL_ERR_NOT_READY.
     * \param in_initiator A valid fabrics initiator
     * \param in_grainIndex The  grain index to transfer. The ordering was given when mxlFabricsRegions object were created. This is true for both
     * local and remote memory regions.
     * \param in_startSlice The start slice in the slice range to transfer. This is inclusive.
     * \param in_endSlice The end slice in the slice range to transfer. This is exclusive.
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorTransferGrain(mxlFabricsInitiator in_initiator, uint64_t in_grainIndex, uint16_t in_startSlice,
        uint16_t in_endSlice);

    /**
     * Enqueue a transfer operation to all added targets. This function is always non-blocking. The transfer operation might be started right
     * away, but is only guaranteed to have completed after mxlFabricsInitiatorMakeProgress*() no longer returns MXL_ERR_NOT_READY.
     * \param in_initiator A valid fabrics initiator
     * \param in_headIndex The head index to transfer.
     * \param in_count The number of samples per channel to transfer.
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFabricsInitiatorTransferSamples(mxlFabricsInitiator in_initiator, uint64_t in_headIndex, size_t in_count);

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
    mxlStatus mxlFabricsTargetInfoToString(mxlFabricsTargetInfo in_targetInfo, char* out_string, size_t* in_stringSize);

    /**
     * Parse a targetInfo object from its string representation.
     * \param in_string A valid string to deserialize
     * \param out_targetInfo A valid target info to deserialize to
     */
    MXL_EXPORT
    mxlStatus mxlFabricsTargetInfoFromString(char const* in_string, mxlFabricsTargetInfo* out_targetInfo);

    /**
     * Free a mxlFabricsTargetInfo object obtained from mxlFabricsTargetSetup() or mxlFabricsTargetInfoFromString().
     * \param in_info A mxlFabricsTargetInfo object
     * \return MXL_STATUS_OK if the mxlFabricsTargetInfo object was freed.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsFreeTargetInfo(mxlFabricsTargetInfo in_info);

#ifdef __cplusplus
}
#endif
