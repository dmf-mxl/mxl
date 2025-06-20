#pragma once

#ifdef __cplusplus
#   include <cstddef>
#   include <cstdint>
#else
#   include <stddef.h>
#   include <stdint.h>
#endif

#include <mxl/flowinfo.h>
#include <mxl/mxl.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * A grain can be marked as invalid for multiple reasons. for example, an input application may have
 * timed out before receiving a grain in time, etc.  Writing grain marked as invalid is the proper way
 * to make the ringbuffer <move forward> whilst letting consumers know that the grain is invalid. A consumer
 * may choose to repeat the previous grain, insert silence, etc.
 */
#define GRAIN_FLAG_INVALID 0x00000001 // 1 << 0.

    /**
     * The payload location of the grain
     */
    typedef enum PayloadLocation
    {
        PAYLOAD_LOCATION_HOST_MEMORY = 0,
        PAYLOAD_LOCATION_DEVICE_MEMORY = 1,
    } PayloadLocation;

    /**
     * A helper type used to describe consecutive sequences of bytes in memory.
     */
    typedef struct BufferSlice
    {
        /** A pointer referring to the beginning of the slice. */
        void const* pointer;
        /** The number of bytes that make up this slice. */
        size_t size;
    } BufferSlice;

    /**
     * A helper type used to describe consecutive sequences of mutable bytes in
     * memory.
     */
    typedef struct MutableBufferSlice
    {
        /** A pointer referring to the beginning of the slice. */
        void* pointer;
        /** The number of bytes that make up this slice. */
        size_t size;
    } MutableBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of bytes
     * in a ring buffer that may potentially straddle the wrapraround
     * point of the buffer.
     */
    typedef struct WrappedBufferSlice
    {
        BufferSlice fragments[2];
    } WrappedBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of mutable bytes in
     * a ring buffer that may potentially straddle the wrapraround point of the
     * buffer.
     */
    typedef struct MutableWrappedBufferSlice
    {
        MutableBufferSlice fragments[2];
    } MutableWrappedBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of bytes
     * in memory in consecutive ring buffers separated by the specified
     * stride of bytes.
     */
    typedef struct WrappedMultiBufferSlice
    {
        WrappedBufferSlice base;

        /**
         * The stride in bytes to get from a position in one buffer
         * to the same position in the following buffer.
         */
        size_t stride;
        /**
         * The number of buffers following the base buffer.
         */
        size_t count;
    } WrappedMultiBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of mutable bytes in
     * memory in consecutive ring buffers separated by the specified stride of
     * bytes.
     */
    typedef struct MutableWrappedMultiBufferSlice
    {
        MutableWrappedBufferSlice base;

        /**
         * The stride in bytes to get from a position in one buffer
         * to the same position in the following buffer.
         */
        size_t stride;
        /**
         * The number of buffers following the base buffer.
         */
        size_t count;
    } MutableWrappedMultiBufferSlice;

    typedef struct GrainInfo
    {
        /// Version of the structure. The only currently supported value is 1
        uint32_t version;
        /// Size of the structure
        uint32_t size;
        /// Grain flags.
        uint32_t flags;
        /// Payload location
        PayloadLocation payloadLocation;
        /// Device index (if payload is in device memory). -1 if on host memory.
        int32_t deviceIndex;
        /// Size in bytes of the complete payload of a grain
        uint32_t grainSize;
        /// How many bytes in the grain are currently valid (commited).  This is typically used when writing slices.
        /// A grain is complete when commitedSize == grainSize
        uint32_t commitedSize;
        /// User data space
        uint8_t userData[4068];
    } GrainInfo;

    typedef struct mxlFlowReader_t* mxlFlowReader;
    typedef struct mxlFlowWriter_t* mxlFlowWriter;

    ///
    /// Create a flow using a json flow definition
    ///
    /// \param[in] instance The mxl instance created using mxlCreateInstance
    /// \param[in] flowDef A flow definition in the NMOS Flow json format.  The flow ID is read from the <id> field of this json object.
    /// \param[in] options Additional options (undefined). \todo Specify and used the additional options.
    /// \param[out] info A pointer to a FlowInfo structure.  If not nullptr, this structure will be updated with the flow information after the flow
    /// is created.
MXL_EXPORT
    mxlStatus mxlCreateFlow(mxlInstance instance, char const* flowDef, char const* options, FlowInfo* info);

    MXL_EXPORT
    mxlStatus mxlDestroyFlow(mxlInstance instance, char const* flowId);

    MXL_EXPORT
    mxlStatus mxlCreateFlowReader(mxlInstance instance, char const* flowId, char const* options, mxlFlowReader* reader);

    MXL_EXPORT
    mxlStatus mxlReleaseFlowReader(mxlInstance instance, mxlFlowReader reader);

    MXL_EXPORT
    mxlStatus mxlCreateFlowWriter(mxlInstance instance, char const* flowId, char const* options, mxlFlowWriter* writer);

    MXL_EXPORT
    mxlStatus mxlReleaseFlowWriter(mxlInstance instance, mxlFlowWriter writer);

    /**
     * Get a copy of the current descriptive header of a Flow
     *
     * \param[in] reader A valid flow reader
     * \param[out] info A valid pointer to a FlowInfo structure. on return, the structure will be updated with a copy of the current flow info value.
     * \return The result code. \see mxlStatus
     */
MXL_EXPORT
    mxlStatus mxlFlowReaderGetInfo(mxlFlowReader reader, FlowInfo* info);

    /**
     * Accessors for a flow grain at a specific index
     *
     * \param[in] reader A valid discrete flow reader.
     * \param[in] index The index of the grain to obtain
     * \param[in] timeoutNs How long should we wait for the grain (in nanoseconds)
     * \param[out] grain The requested GrainInfo structure.
     * \param[out] payload The requested grain payload.
     * \return The result code. \see mxlStatus
     * \note Please note that this function can only be called on readers that
     *      operate on discrete flows. Any attempt to call this function on a
     *      reader that operates on another type of flow will result in an
     *      error.
     */
MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrain(mxlFlowReader reader, uint64_t index, uint64_t timeoutNs, GrainInfo* grain, uint8_t** payload);

    /**
     * Non-blocking accessors for a flow grain at a specific index
     *
     * \param[in] reader A valid flow reader
     * \param[in] index The index of the grain to obtain
     * \param[out] grain The requested GrainInfo structure.
     * \param[out] payload The requested grain payload.
     * \return The result code. \see mxlStatus
     * \note Please note that this function can only be called on readers that
     *      operate on discrete flows. Any attempt to call this function on a
     *      reader that operates on another type of flow will result in an
     *      error.
     */
MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrainNonBlocking(mxlFlowReader reader, uint64_t index, GrainInfo* grain, uint8_t** payload);

    /**
     * Open a grain for mutation.  The flow writer will remember which index is currently opened. Before opening a new grain
     * for mutation, the user must either cancel the mutation using mxlFlowWriterCancelGrain or mxlFlowWriterCommitGrain.
     *
     * \todo Allow operating on multiple grains simultaneously, by making this function return a handle that has to be passed
     *      to mxlFlowWriterCommitGrain or mxlFlowWriterCancelGrain to identify the grain the call refers to.
     *
     * \param[in] writer A valid flow writer
     * \param[in] index The index of the grain to obtain
     * \param[out] grainInfo The requested GrainInfo structure.
     * \param[out] payload The requested grain payload.
     * \return The result code. \see mxlStatus
     * \note Please note that this function can only be called on writers that
     *      operate on discrete flows. Any attempt to call this function on a
     *      writer that operates on another type of flow will result in an
     *      error.
     */
MXL_EXPORT
    mxlStatus mxlFlowWriterOpenGrain(mxlFlowWriter writer, uint64_t index, GrainInfo* grainInfo, uint8_t** payload);

    /**
     *
     * \param[in] writer A valid flow writer
     */
MXL_EXPORT
    mxlStatus mxlFlowWriterCancelGrain(mxlFlowWriter writer);

    /**
     * Inform mxl that a user is done writing the grain that was previously opened.  This will in turn signal all readers waiting on the ringbuffer
     * that a new grain is available.  The graininfo flags field in shared memory will be updated based on grain->flags This will increase the head
     * and potentially the tail IF this grain is the new head.
     *
     * \return The result code. \see mxlStatus
     */
MXL_EXPORT
    mxlStatus mxlFlowWriterCommitGrain(mxlFlowWriter writer, GrainInfo const* grain);

    /**
     * Accessor for a specific set of samples across all channels starting at a
     * specific index.
     *
     * \param[in] index The head index of the samples to obtain.
     * \param[in] count The number of samples to obtain.
     * \param[out] payloadBuffersSlices A pointer to a wrapped multi buffer
     *      slice that represents the requested range across all channel
     *      buffers.
     *
     * \return A status code describing the outcome of the call.
     * \note No guarantees are made as to how long the caller may
     *      safely hang on to the returned range of samples without the
     *      risk of these samples being overwritten.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetSamples(mxlFlowReader reader, uint64_t index, size_t count, WrappedMultiBufferSlice* payloadBuffersSlices);

    /**
     * Open a specific set of mutable samples across all channels starting at a
     * specific index for mutation.
     *
     * \param[in] index The head index of the samples that will be mutated.
     * \param[in] count The number of samples in each channel that will be
     *      mutated.
     * \param[out] payloadBuffersSlices A pointer to a mutable wrapped multi
     *      buffer slice that represents the requested range across all channel
     *      buffers.
     *
     * \return A status code describing the outcome of the call.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterOpenSamples(mxlFlowWriter writer, uint64_t index, size_t count, MutableWrappedMultiBufferSlice* payloadBuffersSlices);

    /**
     * Cancel the mutation of the previously opened range of samples.
     * \param[in] writer A valid flow writer
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterCancelSamples(mxlFlowWriter writer);

    /**
     * Inform mxl that a user is done writing the sample range that was previously opened.
     *
     * \param[in] writer A valid flow writer
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterCommitSamples(mxlFlowWriter writer);
#ifdef __cplusplus
}
#endif