// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file PayloadBackendAbi.h
 *
 * C interface between libmxl and the payload storage backends that the SDK ships as separate shared libraries.
 *
 * This header is internal to the SDK. It is not installed, and applications cannot register backends. libmxl
 * only loads backends by name, from the directory that holds libmxl, so the SDK decides which backends exist
 * and every writer and reader of a flow uses the same backend code. Because the interface is internal it can
 * change between SDK releases. The version fields let libmxl reject a backend library from another release.
 *
 * A backend library is named libmxl-payload-\<name\>.so and exports one C function, named by
 * MXL_PAYLOAD_BACKEND_ENTRY_POINT, with the signature mxlGetPayloadBackendApiFn. Only C types cross the
 * interface. No C++ exception may leave a backend function.
 *
 * Options that only make sense for one kind of storage, such as a CUDA device index or a dma-heap name, are not
 * part of this interface. libmxl passes the keys of the "payload" flow option that it does not interpret itself to
 * the backend as a JSON object, and the backend validates them.
 */

#pragma once

#include <stdint.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Version of the backend interface described by this header. */
#define MXL_PAYLOAD_BACKEND_ABI_VERSION 1U

/** Name of the function every backend library exports. */
#define MXL_PAYLOAD_BACKEND_ENTRY_POINT "mxlGetPayloadBackendApi"

/** Value of mxlPayloadBackendFlowInfo.accessMode for a reader. */
#define MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY 0U

/** Value of mxlPayloadBackendFlowInfo.accessMode for a writer. */
#define MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE 1U

    /** Opaque state that a backend keeps for one flow opened or created in one process. */
    typedef struct mxlPayloadBackendInstance_t* mxlPayloadBackendInstance;

    /**
     * Geometry and access of the flow a backend instance serves. libmxl fills all fields.
     */
    typedef struct mxlPayloadBackendFlowInfo_t
    {
        /** Size of this structure in bytes. */
        uint32_t structSize;
        /** Number of ring buffer slots, equal to the grain count of the flow. */
        uint32_t grainCount;
        /** Size in bytes of the payload of one grain, as recorded in mxlGrainInfo.grainSize. */
        uint64_t grainPayloadSize;
        /** MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY or MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE. */
        uint32_t accessMode;
        /** Number of planes in a grain. Video v210 has 1, v210a has 2 (fill, then key). */
        uint32_t planeCount;
        /** Number of slices in each plane of a grain. For video, the number of lines. */
        uint32_t slicesPerGrain;
        /**
         * Minimum size in bytes of one slice of each plane, from mxlDiscreteFlowConfigInfo.sliceSizes. A backend
         * that allocates images or device buffers may choose a larger pitch and reports it with describeLayout().
         */
        uint32_t sliceSizes[MXL_MAX_PLANES_PER_GRAIN];
        /**
         * The mxlDataFormat of the flow, MXL_DATA_FORMAT_VIDEO or MXL_DATA_FORMAT_DATA. libmxl does not restrict which
         * formats a backend holds, so a backend rejects the formats it cannot store.
         */
        uint32_t format;
    } mxlPayloadBackendFlowInfo;

    /**
     * Function table of a backend, version 1.
     *
     * The table and the strings it points to must stay valid until the process exits. libmxl never unloads a
     * backend library.
     */
    typedef struct mxlPayloadBackendApiV1_t
    {
        /** Size of this structure in bytes, as compiled into the backend. */
        uint32_t structSize;
        /** Must be MXL_PAYLOAD_BACKEND_ABI_VERSION. */
        uint32_t abiVersion;
        /** Backend name. Must be equal to the name libmxl used to load the library. */
        char const* name;
        /** The mxlPayloadStorageType of every slot this backend describes. */
        uint32_t storageType;
        /** Reserved. Must be zero. */
        uint32_t reserved;

        /**
         * Allocate the payload storage of a new flow and write any files that readers need to import it.
         *
         * libmxl calls this while the flow is still in its temporary directory, before the rename that
         * publishes the flow. Files must be written under stagingDir. They appear under finalDir once the
         * flow is published. Values that must refer to the published location, such as keys of a registry
         * inside the process, must be derived from finalDir.
         *
         * \param[in] info The flow geometry. accessMode is MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE.
         * \param[in] options The writer's options for this backend, as JSON object text. Never null, "{}" if there
         *      are none. Only valid during the call.
         * \param[in] stagingDir The temporary flow directory. Null terminated UTF-8.
         * \param[in] finalDir The directory the flow will have once published. Null terminated UTF-8.
         * \param[out] out_instance Receives the new instance on success.
         * \return MXL_STATUS_OK on success, MXL_ERR_INVALID_ARG if the options are invalid, or another error code.
         *      On error, the backend must release everything it allocated. libmxl then removes the temporary
         *      directory.
         */
        mxlStatus (*prepare)(mxlPayloadBackendFlowInfo const* info, char const* options, char const* stagingDir, char const* finalDir,
            mxlPayloadBackendInstance* out_instance);

        /**
         * Create an instance for a flow that already exists.
         *
         * This call must not import device memory or otherwise require a device. Imports happen on the first
         * call to slotStorage(), so that a process can open a flow to read its metadata without a device.
         *
         * \param[in] info The flow geometry and the access this process needs.
         * \param[in] options The reader's options for this backend, as JSON object text, for example the device on
         *      which to access the payload. Never null, "{}" if there are none. Only valid during the call.
         * \param[in] flowDir The published flow directory. Null terminated UTF-8.
         * \param[out] out_instance Receives the new instance on success.
         * \return MXL_STATUS_OK on success, MXL_ERR_INVALID_ARG if the options are invalid, or another error code.
         */
        mxlStatus (*open)(mxlPayloadBackendFlowInfo const* info, char const* options, char const* flowDir, mxlPayloadBackendInstance* out_instance);

        /**
         * Release an instance and everything it holds. Memory created by prepare() is freed. Memory imported by
         * slotStorage() is released.
         *
         * \param[in] instance The instance to release. May be null, in which case the call does nothing.
         */
        void (*destroy)(mxlPayloadBackendInstance instance);

        /**
         * The device on which this process accesses the payload.
         *
         * \param[in] instance A valid instance.
         * \return The index of the device in this process, as numbered by the device API the backend uses, or -1 if
         *      the payload is not tied to a device or the device is not known yet. libmxl records the writer's value in
         *      mxlCommonFlowConfigInfo.deviceIndex.
         */
        int32_t (*deviceIndex)(mxlPayloadBackendInstance instance);

        /**
         * Describe the storage of one slot, mapping the storage first if mapSlots() was not called.
         *
         * May be called from several threads at the same time, including for the first access. The description
         * of a slot must not change for the lifetime of the instance.
         *
         * \param[in] instance A valid instance.
         * \param[in] slot The slot to describe. Less than mxlPayloadBackendFlowInfo.grainCount.
         * \param[out] out_storage Receives the description. The backend fills every byte, including version,
         *      size, storageType and slot.
         * \return MXL_STATUS_OK on success, MXL_ERR_INVALID_ARG if slot is out of range, or another error code
         *      if the payload cannot be imported.
         */
        mxlStatus (*slotStorage)(mxlPayloadBackendInstance instance, uint32_t slot, mxlGrainStorage* out_storage);

        /**
         * Complete the storage layout of the flow. Must not import device memory.
         *
         * libmxl passes a default layout in which the planes follow each other without padding between slices.
         * The backend sets storageType, deviceIndex and deviceUuid. It may change the offset, size and pitch of
         * planes, for example to the values a device chose when it allocated its buffers, and may fill the part
         * of the reserved space defined for its storage type. libmxl checks the result and rejects a layout
         * whose planes cannot hold their slices.
         *
         * \param[in] instance A valid instance.
         * \param[in,out] inout_layout The default layout on input, the complete layout on output.
         * \return MXL_STATUS_OK on success, or an error code.
         */
        mxlStatus (*describeLayout)(mxlPayloadBackendInstance instance, mxlGrainStorageLayout* inout_layout);

        /**
         * Map the storage of every slot into this process now, for example by importing device memory or
         * receiving file descriptors, so that later slotStorage() calls do no further work.
         *
         * libmxl calls this when the application maps the slots of a reader or writer, before it asks for any
         * slot description. It may be called more than once and from several threads. Calls after the first
         * successful one must return MXL_STATUS_OK without doing anything.
         *
         * \param[in] instance A valid instance.
         * \return MXL_STATUS_OK on success, or an error code if the storage cannot be mapped in this process.
         */
        mxlStatus (*mapSlots)(mxlPayloadBackendInstance instance);
    } mxlPayloadBackendApiV1;

    /**
     * Signature of the function a backend library exports under MXL_PAYLOAD_BACKEND_ENTRY_POINT.
     *
     * \param[in] requestedVersion The interface version libmxl was built with.
     * \param[out] out_api Receives the function table of the backend on success.
     * \return MXL_STATUS_OK on success, or MXL_ERR_UNSUPPORTED_OPERATION if the backend does not implement
     *      requestedVersion.
     */
    typedef mxlStatus (*mxlGetPayloadBackendApiFn)(uint32_t requestedVersion, mxlPayloadBackendApiV1 const** out_api);

#ifdef __cplusplus
}
#endif
