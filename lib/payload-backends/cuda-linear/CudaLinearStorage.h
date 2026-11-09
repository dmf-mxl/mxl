// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file CudaLinearStorage.h
 *
 * Storage description of the "cuda-linear" payload backend. The SDK does not interpret the reserved bytes of
 * mxlGrainStorageLayout and mxlGrainStorage for this storage type, because its name has a dot. The backend fills
 * the bytes with the structures below, and readers that use the backend read them back.
 */

#ifndef MXL_CUDA_LINEAR_STORAGE_H
#define MXL_CUDA_LINEAR_STORAGE_H

#include <stdint.h>

/** Storage type of the cuda-linear backend, a reversed domain name so that libmxl copies the reserved bytes as they are. */
#define MXL_CUDA_LINEAR_STORAGE_TYPE "com.nvidia.cuda-device-pointer"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Written in mxlGrainStorageLayout.reserved. The device is identified by its UUID. The index is the number of the
     * device in the process that reads the layout, and is -1 if that process cannot see the device.
     */
    typedef struct mxlCudaLinearLayoutInfo_t
    {
        /** UUID of the device that holds the payload, as CUDA reports it (16 bytes). */
        uint8_t deviceUuid[16];
        /** Device index in the reading process, or -1. */
        int32_t deviceIndex;
        /** Reserved. Always zero. */
        uint32_t reserved0;
    } mxlCudaLinearLayoutInfo;

    /**
     * Written in mxlGrainStorage.reserved. The device address of the first byte of the slot, valid in the process
     * that holds the mapping.
     */
    typedef struct mxlCudaLinearSlotStorage_t
    {
        /** CUDA device pointer to the slot, valid in the calling process. */
        void* devicePointer;
    } mxlCudaLinearSlotStorage;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#   include <cstring>
#   include <mxl/flow.h>

/**
 * \param[in] layout A layout whose storage type is MXL_CUDA_LINEAR_STORAGE_TYPE.
 * \return The device that holds the payload.
 */
inline mxlCudaLinearLayoutInfo mxlCudaLinearLayout(mxlGrainStorageLayout const& layout) noexcept
{
    auto info = mxlCudaLinearLayoutInfo{};
    std::memcpy(&info, layout.reserved, sizeof(info));
    return info;
}

/**
 * \param[in] slot A slot whose storage type is MXL_CUDA_LINEAR_STORAGE_TYPE.
 * \return The CUDA device pointer to the slot, valid in the calling process.
 */
inline void* mxlCudaLinearDevicePointer(mxlGrainStorage const& slot) noexcept
{
    auto storage = mxlCudaLinearSlotStorage{};
    std::memcpy(&storage, slot.reserved, sizeof(storage));
    return storage.devicePointer;
}

#endif

#endif
