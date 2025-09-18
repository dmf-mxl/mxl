// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
#   include <cstddef>
#   include <cstdint>
#else
#   include <stdbool.h>
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
#define MXL_GRAIN_FLAG_INVALID 0x00000001 // 1 << 0.

    /**
     * The payload location of the grain
     */
    typedef enum mxlPayloadLocation
    {
        MXL_PAYLOAD_LOCATION_HOST_MEMORY = 0,
        MXL_PAYLOAD_LOCATION_DEVICE_MEMORY = 1,
    } mxlPayloadLocation;

    /**
     * A helper type used to describe consecutive sequences of bytes in memory.
     */
    typedef struct mxlBufferSlice_t
    {
        /** A pointer referring to the beginning of the slice. */
        void const* pointer;
        /** The number of bytes that make up this slice. */
        size_t size;
    } mxlBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of mutable bytes in
     * memory.
     */
    typedef struct mxlMutableBufferSlice_t
    {
        /** A pointer referring to the beginning of the slice. */
        void* pointer;
        /** The number of bytes that make up this slice. */
        size_t size;
    } mxlMutableBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of bytes
     * in a ring buffer that may potentially straddle the wrapraround
     * point of the buffer.
     */
    typedef struct mxlWrappedBufferSlice_t
    {
        mxlBufferSlice fragments[2];
    } mxlWrappedBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of mutable bytes in
     * a ring buffer that may potentially straddle the wrapraround point of the
     * buffer.
     */
    typedef struct mxlMutableWrappedBufferSlice_t
    {
        mxlMutableBufferSlice fragments[2];
    } mxlMutableWrappedBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of bytes
     * in memory in consecutive ring buffers separated by the specified
     * stride of bytes.
     */
    typedef struct mxlWrappedMultiBufferSlice_t
    {
        mxlWrappedBufferSlice base;

        /**
         * The stride in bytes to get from a position in one buffer
         * to the same position in the following buffer.
         */
        size_t stride;
        /**
         * The total number of buffers in the sequence.
         */
        size_t count;
    } mxlWrappedMultiBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of mutable bytes in
     * memory in consecutive ring buffers separated by the specified stride of
     * bytes.
     */
    typedef struct mxlMutableWrappedMultiBufferSlice_t
    {
        mxlMutableWrappedBufferSlice base;

        /**
         * The stride in bytes to get from a position in one buffer
         * to the same position in the following buffer.
         */
        size_t stride;
        /**
         * The total number of buffers in the sequence.
         */
        size_t count;
    } mxlMutableWrappedMultiBufferSlice;

    typedef struct mxlGrainInfo_t
    {
        /// Version of the structure. The only currently supported value is 1
        uint32_t version;
        /// Size of the structure
        uint32_t size;
        /// Grain flags.
        uint32_t flags;
        /// Payload location
        mxlPayloadLocation payloadLocation;
        /// Device index (if payload is in device memory). -1 if on host memory.
        int32_t deviceIndex;
        /// Size in bytes of the complete payload of a grain
        uint32_t grainSize;
        /// How many bytes in the grain are currently valid (commited).  This is typically used when writing slices.
        /// A grain is complete when commitedSize == grainSize
        uint32_t commitedSize;
        /// User data space
        uint8_t userData[4068];
    } mxlGrainInfo;

    typedef struct mxlFlowReader_t* mxlFlowReader;
    typedef struct mxlFlowWriter_t* mxlFlowWriter;

    ///
    /// Create a flow using a json flow definition
    ///
    /// \param[in] instance The mxl instance created using mxlCreateInstance
    /// \param[in] flowDef A flow definition in the NMOS Flow json format.  The flow ID is read from the <id> field of this json object.
    /// \param[in] options Additional options (undefined). \todo Specify and used the additional options.
    /// \param[out] info A pointer to an mxlFlowInfo structure.  If not nullptr, this structure will be updated with the flow information after the
    /// flow is created.
    MXL_EXPORT
    mxlStatus mxlCreateFlow(mxlInstance instance, char const* flowDef, char const* options, mxlFlowInfo* info);

    MXL_EXPORT
    mxlStatus mxlDestroyFlow(mxlInstance instance, char const* flowId);

    ///
    /// Verify if a flow has an active writer or not
    ///
    /// \param[in] instance The mxl instance created using mxlCreateInstance
    /// \param[in] flowId The ID of the flow to check.
    /// \param[out] isActive A pointer to a boolean that will be set to true if the flow has an active writer, false otherwise.
    /// \return MXL_STATUS_OK if the flow exists and has been tested successfully, or an error code otherwise.
    MXL_EXPORT
    mxlStatus mxlIsFlowActive(mxlInstance instance, char const* flowId, bool* isActive);

    /**
     * Get the flow definition used to create the given flow.
     *
     * @param[in] instance The mxl instance tied to domain we want to get the flow definition from. If invalid instance is provided, the function will
     *                     return MXL_ERR_INVALID_ARG and do nothing.
     * @param[in] flowId The id of the flow we want to get the definition for. If nullptr, the function will return MXL_ERR_INVALID_ARG and do
     *                   nothing.
     * @param[out] buffer A pointer to a buffer that will be filled with the flow definition. If nullptr, or if the buffer does not contain enough
     *                    space to return the result (based on the bufferSize provided later), the function will return MXL_ERR_INVALID_ARG and
     *                    update the value pointed to by the bufferSize with the required buffer size.
     * @param[in,out] bufferSize A pointer to a variable with the length of the supplied buffer. If nullptr, the function will return
     *                           MXL_ERR_INVALID_ARG and do nothing. If function succeeds, the value pointed to by this variable will be updated with
     *                           the number of bytes written to the buffer (including the null terminator).
     * @return MXL_STATUS_OK when buffer was successfully filled with the flow definition, or other error codes based on the previous parameter
     *         description or other encountered errors.
     */
    MXL_EXPORT
    mxlStatus mxlGetFlowDef(mxlInstance instance, char const* flowId, char* buffer, size_t* bufferSize);

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
     * \param[out] info A valid pointer to an mxlFlowInfo structure. on return, the structure will be updated with a copy of the current flow info
     * value.
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetInfo(mxlFlowReader reader, mxlFlowInfo* info);

    /**
     * Accessors for a flow grain at a specific index
     *
     * \param[in] reader A valid discrete flow reader.
     * \param[in] index The index of the grain to obtain
     * \param[in] timeoutNs How long should we wait for the grain (in nanoseconds)
     * \param[out] grain The requested mxlGrainInfo structure.
     * \param[out] payload The requested grain payload.
     * \return The result code. \see mxlStatus
     * \note Please note that this function can only be called on readers that
     *      operate on discrete flows. Any attempt to call this function on a
     *      reader that operates on another type of flow will result in an
     *      error.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrain(mxlFlowReader reader, uint64_t index, uint64_t timeoutNs, mxlGrainInfo* grain, uint8_t** payload);

    /**
     * Non-blocking accessors for a flow grain at a specific index
     *
     * \param[in] reader A valid flow reader
     * \param[in] index The index of the grain to obtain
     * \param[out] grain The requested mxlGrainInfo structure.
     * \param[out] payload The requested grain payload.
     * \return The result code. \see mxlStatus
     * \note Please note that this function can only be called on readers that
     *      operate on discrete flows. Any attempt to call this function on a
     *      reader that operates on another type of flow will result in an
     *      error.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrainNonBlocking(mxlFlowReader reader, uint64_t index, mxlGrainInfo* grain, uint8_t** payload);

    /**
     * Open a grain for mutation.  The flow writer will remember which index is currently opened. Before opening a new grain
     * for mutation, the user must either cancel the mutation using mxlFlowWriterCancelGrain or mxlFlowWriterCommitGrain.
     *
     * \todo Allow operating on multiple grains simultaneously, by making this function return a handle that has to be passed
     *      to mxlFlowWriterCommitGrain or mxlFlowWriterCancelGrain to identify the grain the call refers to.
     *
     * \param[in] writer A valid flow writer
     * \param[in] index The index of the grain to obtain
     * \param[out] mxlGrainInfo The requested mxlGrainInfo structure.
     * \param[out] payload The requested grain payload.
     * \return The result code. \see mxlStatus
     * \note Please note that this function can only be called on writers that
     *      operate on discrete flows. Any attempt to call this function on a
     *      writer that operates on another type of flow will result in an
     *      error.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterOpenGrain(mxlFlowWriter writer, uint64_t index, mxlGrainInfo* mxlGrainInfo, uint8_t** payload);

    /**
     *
     * \param[in] writer A valid flow writer
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterCancelGrain(mxlFlowWriter writer);

    /**
     * Inform mxl that a user is done writing the grain that was previously opened.  This will in turn signal all readers waiting on the ringbuffer
     * that a new grain is available.  The mxlGrainInfo flags field in shared memory will be updated based on grain->flags This will increase the head
     * and potentially the tail IF this grain is the new head.
     *
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterCommitGrain(mxlFlowWriter writer, mxlGrainInfo const* grain);

    /**
     * Accessor for a specific set of samples across all channels ending at a
     * specific index (`count` samples up to `index`).
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
    mxlStatus mxlFlowReaderGetSamples(mxlFlowReader reader, uint64_t index, size_t count, mxlWrappedMultiBufferSlice* payloadBuffersSlices);

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
    mxlStatus mxlFlowWriterOpenSamples(mxlFlowWriter writer, uint64_t index, size_t count, mxlMutableWrappedMultiBufferSlice* payloadBuffersSlices);

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
