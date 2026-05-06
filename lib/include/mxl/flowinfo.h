// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
#   include <cstdint>
#else
#   include <stdint.h>
#endif

#include <sys/types.h>
#include <mxl/dataformat.h>
#include <mxl/rational.h>

/**
 * Maximum number of planes per grain supported for a continuous flow.
 * 4 planes should be enough for any foreseeable use cases.
 *
 * The current video formats supported by MXL use 1 or 2 planes:
 * - video/v210 flow will have only 1 plane out of MXL_MAX_PLANES_PER_GRAIN
 * - video/v210a flow will have 2 planes out of MXL_MAX_PLANES_PER_GRAIN
 */
#define MXL_MAX_PLANES_PER_GRAIN 4

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * The payload location of the grain
     */
    typedef enum mxlPayloadLocation
    {
        MXL_PAYLOAD_LOCATION_HOST_MEMORY = 0,
        MXL_PAYLOAD_LOCATION_DEVICE_MEMORY = 1,
    } mxlPayloadLocation;

    /**
     * Immutable metadata about a media flow that is independent of the data
     * format of the flow and thus common to all flows handled by MXL.
     */
    typedef struct mxlCommonFlowConfigInfo_t
    {
        /** The flow UUID.  This should be identical to the {flowId} path component. */
        uint8_t id[16];

        /**
         * The data format of this flow.
         * \see mxlDataFormat
         */
        uint32_t format;

        /** No flags defined yet. */
        uint32_t flags;

        /**
         * The number of grains per second expressed as a rational.
         * For VIDEO and DATA this value must match the 'grain_rate' found in the flow descriptor.
         * For AUDIO flows this value must match the 'sample_rate' found in the flow descriptor.
         */
        mxlRational grainRate;

        /**
         * The largest expected batch size in samples (for continuous flows) or slices (for discrete flows), in which new data is written to this this
         * flow by its producer. For continuous flows, this value must be less than half of the buffer length. For discrete flows, this must be
         * greater or equal to 1.
         */
        uint32_t maxCommitBatchSizeHint;

        /**
         * The largest expected batch size in samples (for continuous flows) or slices (for discrete flows), at which availability of new data is
         * signaled to waiting consumers. This must be a multiple of the commit batch size greater or equal to 1.
         */
        uint32_t maxSyncBatchSizeHint;

        /**
         * And indication, where the payload memory is located.
         * \see mxlPayloadLocation
         */
        uint32_t payloadLocation;

        /**
         * Device index (if payload is in device memory). -1 if on host memory.
         */
        int32_t deviceIndex;

        /**
         * Reserved space for future extensions, padding the total size of this
         * structure to 128 bytes.
         */
        uint8_t reserved[72];
    } mxlCommonFlowConfigInfo;

    /**
     * Immutable metadata about a VIDEO or DATA flo.
     */
    typedef struct mxlDiscreteFlowConfigInfo_t
    {
        /**
         * Length of a slice in bytes. A slice refers to the elemental data type that can be written and comitted to a grain.
         * For video, this is a line of a V210 picture including any padding. For data, this is just a single byte.
         */
        uint32_t sliceSizes[MXL_MAX_PLANES_PER_GRAIN];

        /**
         * How many grains in the ring buffer. This should be identical to the number of entries in the {mxlDomain}/{flowId}/grains/ folder.
         * Accessing the shared memory section for that specific grain should be predictable.
         */
        uint32_t grainCount;

        /**
         * Reserved space for future extensions, padding the total size of this
         * structure to 64 bytes.
         */
        uint8_t reserved[44];
    } mxlDiscreteFlowConfigInfo;

    /**
     * Immutable metadata about an AUDIO flow.
     */
    typedef struct mxlContinuousFlowConfigInfo_t
    {
        /**
         * The number of channels in this flow.
         * A dedicated ring buffer is provided for each channel.
         */
        uint32_t channelCount;

        /**
         * The number of samples in each of the ring buffers.
         * Please be aware that it is only ever possible to read or write a fraction of this buffer, whose upper bound
         * can be obtained by calling mxlFlowReaderGetMaxReadLengthSamples() or mxlFlowWriterGetMaxWriteLengthSamples().
         */
        uint32_t bufferLength;

        /**
         * Reserved space for future extensions, padding the total size of this
         * structure to 64 bytes.
         */
        uint8_t reserved[56];
    } mxlContinuousFlowConfigInfo;

    /**
     * Immutable metadata about a media flow.
     */
    typedef struct mxlFlowConfigInfo_t
    {
        mxlCommonFlowConfigInfo common;

        /** Format specific metadata. */
        union
        {
            mxlDiscreteFlowConfigInfo discrete;
            mxlContinuousFlowConfigInfo continuous;
        };

    } mxlFlowConfigInfo;

    /**
     * Mutable metadata about a media flow that is independent of the data
     * format of the flow and thus common to all flows handled by MXL.
     */
    typedef struct mxlFlowRuntimeInfo_t
    {
        /** The current head index of the ringbuffer(s) of this flow. */
        uint64_t headIndex;

        /** The last time a producer wrote to the flow in nanoseconds since the epoch. */
        uint64_t lastWriteTime;

        /** The last time a consumer read from the flow in nanoseconds since the epoch. */
        uint64_t lastReadTime;

        /**
         * Reserved space for future extensions, padding the total size of this
         * structure to 64 bytes.
         */
        uint8_t reserved[40];
    } mxlFlowRuntimeInfo;

    /**
     * Binary structure stored in the Flow shared memory segment.
     * The flow shared memory will be located in {mxlDomain}/{flowId}
     * where {mxlDomain} is a filesystem location available to the application
     */
    typedef struct mxlFlowInfo_t
    {
        /** Version of this structure. The only currently supported value is 1 */
        uint32_t version;

        /** The total size of this structure */
        uint32_t size;

        mxlFlowConfigInfo config;

        mxlFlowRuntimeInfo runtime;

        /**
         * Padding to get the the total size of this structure to 2048 bytes.
         * Do not use!
         */
        uint8_t reserved[1784];
    } mxlFlowInfo;

#ifdef __cplusplus
}
#endif
