#pragma once

#ifdef __cplusplus
#   include <cstdint>
#else
#   include <stdint.h>
#endif

#include <mxl/dataformat.h>
#include <mxl/rational.h>

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * Metadata about media a flow that is independent of the data format of the
     * flow and thus common to all flows handled by MXL.
     */
    typedef struct CommonFlowInfo
    {
        /** The flow UUID.  This should be identical to the {flowId} path component. */
        uint8_t id[16];

        /** The last time a producer wrote to the flow in nanoseconds since the epoch. */
        uint64_t lastWriteTime;

        /** The last time a consumer read from the flow in nanoseconds since the epoch. */
        uint64_t lastReadTime;

        /**
         * The data format of this flow.
         * \see mxlDataFormat
         */
        uint32_t format;

        /** No flags defined yet. */
        uint32_t flags;

        /** Reserved space for future extensions.  */
        uint8_t reserved[80];
    } CommonFlowInfo;

    typedef struct DiscreteFlowInfo
    {
        /**
         * The number of grains per second expressed as a rational.
         * For VIDEO and DATA this value must match the 'grain_rate' found in the flow descriptor.
         */
        Rational grainRate;

        /**
         * How many grains in the ring buffer. This should be identical to the number of entries in the {mxlDomain}/{flowId}/grains/ folder.
         * Accessing the shared memory section for that specific grain should be predictable.
         */
        uint32_t grainCount;

        /**
         * 32 bit word used syncronization between a writer and multiple readers.  This value can be used by futexes.
         * When a FlowWriter commits some data (a grain, a slice, etc) it will increment this value and then wake all FlowReaders waiting on this
         * memory address.
         */
        uint32_t syncCounter;

        /** The current head index of the ringbuffer. */
        uint64_t headIndex;

        /** Reserved space for future extensions.  */
        uint8_t reserved[96];
    } DiscreteFlowInfo;

    typedef struct ContinuousFlowInfo
    {
        /**
         * The number of samples per second in this continuous flow.
         * For AUDIO flows this value must match the 'sample_rate' found in the flow descriptor.
         */
        Rational sampleRate;

        /**
         * The number of channels in this flow.
         * A dedicated ring buffer is provided for each channel.
         */
        uint32_t channelCount;

        /**
         * The number of samples in each of the ring buffers.
         */
        uint32_t bufferLength;

        /**
         * The largest expected batch size in samples, in which new data is written to this this flow by its producer.
         * This value must be less than half of the buffer length.
         */
        uint32_t commitBatchSize;

        /**
         * The largest expected batch size in samples, at which availability of new data is signaled to waiting consumers.
         * This must be a multiple of the commit batch size greater or equal to 1.
         * \todo Will quite probably be obsoleted by new timing model.
         */
        uint32_t syncBatchSize;

        /** The current head index within the per channel ring buffers. */
        uint64_t headIndex;

        /** Reserved space for future extensions.  */
        uint8_t reserved[88];
    } ContinuousFlowInfo;

    /**
     * Binary structure stored in the Flow shared memory segment.
     * The flow shared memory will be located in {mxlDomain}/{flowId}
     * where {mxlDomain} is a filesystem location available to the application
     */
    typedef struct FlowInfo
    {
        /** Version of this structure. The only currently supported value is 1 */
        uint32_t version;

        /** The total size of this structure */
        uint32_t size;

        CommonFlowInfo common;

        /** Format specific header data. */
        union
        {
            DiscreteFlowInfo discrete;
            ContinuousFlowInfo continuous;
        };

        /** User data space. */
        uint8_t userData[3840];
    } FlowInfo;

#ifdef __cplusplus
}
#endif
