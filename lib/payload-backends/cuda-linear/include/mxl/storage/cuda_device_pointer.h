// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file cuda_device_pointer.h
 *
 * The storage type of payload in CUDA device memory, and the meaning it gives to the reserved bytes of
 * mxlGrainStorageLayout and mxlGrainStorage. The cuda-linear payload backend stores its flows with this type. libmxl
 * passes those bytes through without interpreting them, and knows nothing of CUDA: this header is installed with the
 * backend, not with libmxl. Applications read and write the bytes with the functions below.
 *
 * A slot is addressed through a CUDA device pointer that is valid in the calling process, on the device that the layout
 * reports. The planes are at the offsets of the layout. Device work that writes a grain must be complete before
 * mxlFlowWriterCommitGrain() is called, for example after cudaStreamSynchronize().
 *
 * The fields hold CUDA values in plain C types, so that this header does not depend on the CUDA headers.
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include <mxl/flow.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** The storage type of payload in CUDA device memory, which the cuda-linear payload backend reports. */
#define MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER "cuda-device-pointer"

    /**
     * The part of mxlGrainStorageLayout that describes the devices of the payload: the device that holds it, and the
     * device on which this process accesses it. They differ when a reader accesses the payload from another GPU
     * through peer access.
     */
    typedef struct mxlCudaGrainStorageLayout_t
    {
        /**
         * Index, in the calling process, of the CUDA device on which this process accesses the payload, or -1 if this
         * process has no such device. Device indices differ between processes, for example when CUDA_VISIBLE_DEVICES
         * differs, so the index is only valid in the process that reports it.
         */
        int32_t deviceIndex;
        /** Reserved. Always zero. */
        uint32_t reserved0;
        /** cudaDeviceProp::uuid of the device that holds the payload, which identifies it across processes. */
        uint8_t deviceUuid[16];
        /** Reserved. Always zero. Pads the structure to the reserved bytes of mxlGrainStorageLayout. */
        uint8_t reserved[280];
    } mxlCudaGrainStorageLayout;

    /**
     * The part of mxlGrainStorage that describes the storage of one slot.
     */
    typedef struct mxlCudaGrainStorage_t
    {
        /** CUDA device address of the first byte of the slot storage, valid in the calling process. */
        void* pointer;
    } mxlCudaGrainStorage;

#ifdef __cplusplus
    static_assert(sizeof(mxlCudaGrainStorageLayout) == sizeof(mxlGrainStorageLayout::reserved),
        "mxlCudaGrainStorageLayout must fill the reserved bytes.");
    static_assert(sizeof(mxlCudaGrainStorage) <= sizeof(mxlGrainStorage::reserved), "mxlCudaGrainStorage must fit the reserved bytes.");
#else
_Static_assert(sizeof(mxlCudaGrainStorageLayout) == sizeof(((mxlGrainStorageLayout*)0)->reserved),
    "mxlCudaGrainStorageLayout must fill the reserved bytes.");
_Static_assert(sizeof(mxlCudaGrainStorage) <= sizeof(((mxlGrainStorage*)0)->reserved), "mxlCudaGrainStorage must fit the reserved bytes.");
#endif

    /**
     * \param[in] layout A layout whose storage type is MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER.
     * \return The devices of the payload.
     */
    inline static mxlCudaGrainStorageLayout mxlGetCudaGrainStorageLayout(mxlGrainStorageLayout const* layout)
    {
        mxlCudaGrainStorageLayout result;
        memcpy(&result, layout->reserved, sizeof(result));
        return result;
    }

    /**
     * Store the device that holds the payload in a layout.
     * \param[in,out] layout The layout.
     * \param[in] value The device.
     */
    inline static void mxlSetCudaGrainStorageLayout(mxlGrainStorageLayout* layout, mxlCudaGrainStorageLayout const* value)
    {
        memcpy(layout->reserved, value, sizeof(*value));
    }

    /**
     * \param[in] storage The description of a slot of a flow whose storage type is MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER.
     * \return The storage of the slot.
     */
    inline static mxlCudaGrainStorage mxlGetCudaGrainStorage(mxlGrainStorage const* storage)
    {
        mxlCudaGrainStorage result;
        memcpy(&result, storage->reserved, sizeof(result));
        return result;
    }

    /**
     * Store the storage of a slot in its description.
     * \param[in,out] storage The slot description.
     * \param[in] value The storage of the slot.
     */
    inline static void mxlSetCudaGrainStorage(mxlGrainStorage* storage, mxlCudaGrainStorage const* value)
    {
        memcpy(storage->reserved, value, sizeof(*value));
    }

#ifdef __cplusplus
}
#endif
