// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

/** \brief This module enables users to integrate custom buffers with the MXL Fabrics API.
 *Custom buffers can be registered with MXL Fabrics by defining their memory regions and describing the flow using mxlFabricsExtRegionsConfig.
 * Each memory region maps to a single grain. Users can then instantiate an mxlFabricsRegions object by invoking mxlFabricsExtGetRegions. When custom
 * regions use the same data layout as MXL, users can directly leverage the standard fabrics.h API for data transfers and reads. However, if the
 * layout differs, users must use `mxlFabricsExtInitiatorTransferGrain` instead of `mxlFabricsInitiatorTransferGrain` for data transfer operations.
 * The rest of the API remains valid.
 */

#ifndef __cplusplus
#else
#endif

#include <mxl/fabrics.h>
#include <mxl/flowinfo.h>
#include <mxl/platform.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** Configuration for a user defined memory region location.
     */
    typedef struct mxlFabricsExtMemoryRegionLocation_t
    {
        mxlPayloadLocation type; /**< Memory type of the payload. */
        uint64_t deviceId;       /**< Device Index when device memory is used, otherwise it is ignored. */
    } mxlFabricsExtMemoryRegionLocation;

    /** Configuration for a user defined memory region.
     */
    typedef struct mxlFabricsExtMemoryRegion_t
    {
        uintptr_t addr;                        /**< Start address of the contiguous memory region. */
        size_t size;                           /**< Size of that memory region */
        mxlFabricsExtMemoryRegionLocation loc; /**< Location information for that memory region. */
    } mxlFabricsExtMemoryRegion;

    /** User configuration for a collection of user defined memory regions.
     */
    typedef struct mxlFabricsExtRegionsConfig_t
    {
        mxlFabricsExtMemoryRegion const* regions;     /**< Pointer to an array of memory regions. */
        size_t regionsCount;                          /**< The number of memory regions in the array. */

        uint32_t sliceSize[MXL_MAX_PLANES_PER_GRAIN]; /**< The size of a single slice in bytes. */

        mxlDataFormat format;                         /**< The data format representing these regions. */
    } mxlFabricsExtRegionsConfig;

    /**
     * Create a regions object from a list of memory region groups.
     * \param in_config User configuiration for the memory regions.
     * \param out_regions Returns a pointer to the created regions object. The user is responsible for freeing this object by calling
     * `mxlFabricsRegionsFree()`.
     * \return MXL_STATUS_OK if the regions object was successfully created.
     */
    MXL_EXPORT
    mxlStatus mxlFabricsExtGetRegions(mxlFabricsExtRegionsConfig const* in_config, mxlFabricsRegions* out_regions);

    /**
     * Enqueue a transfer operation to a specific target. This function is always non-blocking. The transfer operation might be started right
     * away, but is only guaranteed to have completed after mxlFabricsInitiatorMakeProgress*() no longer returns MXL_ERR_NOT_READY.
     * \param in_initiator A valid fabrics initiator
     * \param in_targetInfo The target information of the specific target. This should be the same as the one returned from "mxlFabricsTargetSetup".
     * \param in_localIndex The index of the memory region (local) to transfer. The ordering was given when mxlFabricsRegions object was created.
     * \param in_remoteIndex The index of the memory region (remote) to receive the transfer at the target side. The ordering was given when
     * mxlFabricsRegions object was created.
     * \param in_payloadOffset Offset in bytes inside the remote memory region before the payload starts. This is typically the header if any. In MXL,
     * this corresponds to MXL_GRAIN_PAYLOAD_OFFSET.
     * \param in_startSlice The start slice in the slice range to transfer. This is inclusive.
     * \param in_endSlice The end slice in the slice range to transfer. This is exclusive.
     * \return The result code. \see mxlStatus
     * \note This function is useful when the underlying buffer layout does not match the MXL grain data layout. Otherwise \see
     * mxlFabricsInitiatorTransferGrain()
     */
    MXL_EXPORT
    mxlStatus mxlFabricsExtInitiatorTransferGrain(mxlFabricsInitiator in_initiator, mxlFabricsTargetInfo const in_targetInfo, uint64_t in_localIndex,
        uint64_t in_remoteIndex, uint64_t in_payloadOffset, uint16_t in_startSlice, uint16_t in_endSlice);

#ifdef __cplusplus
}
#endif
