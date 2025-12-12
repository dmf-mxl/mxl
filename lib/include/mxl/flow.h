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
 * A grain can be marked as invalid for multiple reasons. For example, an input application may have
 * timed out before receiving a grain in time, etc.  Writing grain marked as invalid is the proper way
 * to make the ringbuffer <move forward> whilst letting consumers know that the grain is invalid. A consumer
 * may choose to repeat the previous grain, insert silence, etc.
 */
#define MXL_GRAIN_FLAG_INVALID 0x00000001 // 1 << 0.

/**
 * A symbolic constant that may be passed to functions as their "min valid slices" parameter, in order to
 * indicate that no minimum is imposed and any number of valid slices will do.
 */
#define MXL_GRAIN_VALID_SLICES_ANY ((uint16_t)0)

/**
 * A symbolic constant that may be passed to functions as their "min valid slices" parameter, in order to
 * indicate that all slices need to be available and partial grains are not acceptable.
 */
#define MXL_GRAIN_VALID_SLICES_ALL ((uint16_t)UINT16_MAX)

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
     * in a ring buffer that may potentially straddle the wraparound
     * point of the buffer.
     */
    typedef struct mxlWrappedBufferSlice_t
    {
        mxlBufferSlice fragments[2];
    } mxlWrappedBufferSlice;

    /**
     * A helper type used to describe consecutive sequences of mutable bytes in
     * a ring buffer that may potentially straddle the wraparound point of the
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
        /// Version of the structure. The only currently supported value is 2
        uint32_t version;
        /// Size of the structure
        uint32_t size;

        /// Epoch Grain index used by that ring buffer entry.
        uint64_t index;

        /// Grain flags.
        uint32_t flags;
        /// Size in bytes of the complete payload of a grain
        uint32_t grainSize;
        /// Number of slices that make up a full grain. A slice is the elemental data type that can be committed to a grain. For video, this is a
        /// single line of a picture in the specified format. For data, this is a byte of data.
        uint16_t totalSlices;
        /// How many slices of the grain are currently valid (committed). This is typically used when writing individual slices instead of a full
        /// grain. A grain is complete when validSlices == totalSlices
        uint16_t validSlices;
        /// Padding. Do not use.
        uint8_t reserved[4068];
    } mxlGrainInfo;

    typedef struct mxlFlowReader_t* mxlFlowReader;
    typedef struct mxlFlowWriter_t* mxlFlowWriter;

    typedef struct mxlFlowSynchronizationGroup_t* mxlFlowSynchronizationGroup;

    /**
     * Attempt to create a flow writer for a given flow definition. If the flow does not exist already, it is created and 'created' will be set to
     * true. If the flow exists, it will be opened and 'created' will be set to false. If the flow exists already it is not guaranteed that the flow
     * definition of the existing flow is exactly equal to the flow definition supplied in flowDef. The definition of the existing flow can be
     * retrieved using mxlGetFlowDef().
     * \param[in] instance The mxl instance created using mxlCreateInstance
     * \param[in] flowDef The flow definition from which a flow should be created if there is not already a flow with the same flow id.
     * \param[in] options (optional) Additional options, can be NULL
     * \param[out] writer A pointer to a memory location where the created flow writer will be written.
     * \param[out] configInfo (optional) A pointer to an mxlFlowConfigInfo structure.
     *     If not the null pointer, this structure will be updated with the flow information after the flow is created.
     * \param[out] created (optional) A pointer to a boolean.
     *     If not the null pointer, this variable will be set to true if a new flow was created, and to false if an existing flow was opened instead.
     */
    MXL_EXPORT
    mxlStatus mxlCreateFlowWriter(mxlInstance instance, char const* flowDef, char const* options, mxlFlowWriter* writer,
        mxlFlowConfigInfo* configInfo, bool* created);

    MXL_EXPORT
    mxlStatus mxlReleaseFlowWriter(mxlInstance instance, mxlFlowWriter writer);

    MXL_EXPORT
    mxlStatus mxlCreateFlowReader(mxlInstance instance, char const* flowId, char const* options, mxlFlowReader* reader);

    MXL_EXPORT
    mxlStatus mxlReleaseFlowReader(mxlInstance instance, mxlFlowReader reader);

    /**
     * Verify if a flow has an active writer or not
     *
     * \param[in] instance The mxl instance created using mxlCreateInstance
     * \param[in] flowId The ID of the flow to check.
     * \param[out] isActive A pointer to a boolean that will be set to true if the flow has an active writer, false otherwise.
     * \return MXL_STATUS_OK if the flow exists and has been tested successfully, or an error code otherwise.
     */
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

    /**
     * Get a copy of the header of a Flow
     *
     * \param[in] reader A valid flow reader
     * \param[out] info A valid pointer to an mxlFlowInfo structure.
     *      On return, the structure will be updated with a copy of the current
     *      flow config info value.
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetInfo(mxlFlowReader reader, mxlFlowInfo* info);

    /**
     * Get a copy of the immutable descriptive header of a Flow
     *
     * \param[in] reader A valid flow reader
     * \param[out] info A valid pointer to an mxlFlowConfigInfo structure.
     *      On return, the structure will be updated with a copy of the current
     *      flow config info value.
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetConfigInfo(mxlFlowReader reader, mxlFlowConfigInfo* info);

    /**
     * Get a copy of the current runtime header of a Flow
     *
     * \param[in] reader A valid flow reader
     * \param[out] info A valid pointer to an mxlFlowRuntimeInfo structure.
     *      On return, the structure will be updated with a copy of the current
     *      flow config info value.
     * \return The result code. \see mxlStatus
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetRuntimeInfo(mxlFlowReader reader, mxlFlowRuntimeInfo* info);

    /**
     * Accessors for a flow grain at a specific index
     * This method is expected to wait until the full grain is available (or the timeout expires). For partial grain access use
     * mxlFlowReaderGetGrainSlice()
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
     * Accessors for a flow grain at a specific index, with a minimum number of valid slices.
     *
     * \param[in] reader A valid discrete flow reader.
     * \param[in] index The index of the grain to obtain
     * \param[in] minValidSlices The minimum number of valid slices required in the returned grain.
     * \param[in] timeoutNs How long should we wait for the slice (in nanoseconds)
     * \param[out] grain The requested mxlGrainInfo structure.
     * \param[out] payload The requested grain payload.
     * \return The result code. \see mxlStatus
     * \note Please note that this function can only be called on readers that
     *      operate on discrete flows. Any attempt to call this function on a
     *      reader that operates on another type of flow will result in an
     *      error.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrainSlice(mxlFlowReader reader, uint64_t index, uint16_t minValidSlices, uint64_t timeoutNs, mxlGrainInfo* grain,
        uint8_t** payload);

    /**
     * Non-blocking accessor for a flow grain at a specific index
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
     * Non-blocking accessor for a flow grain at a specific index, with a minimum number of valid slices.
     *
     * \param[in] reader A valid discrete flow reader.
     * \param[in] index The index of the grain to obtain
     * \param[in] minValidSlices The minimum number of valid slices required in the returned grain.
     * \param[out] grain The requested mxlGrainInfo structure.
     * \param[out] payload The requested grain payload.
     * \return The result code. \see mxlStatus
     * \note Please note that this function can only be called on readers that
     *      operate on discrete flows. Any attempt to call this function on a
     *      reader that operates on another type of flow will result in an
     *      error.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrainSliceNonBlocking(mxlFlowReader reader, uint64_t index, uint16_t minValidSlices, mxlGrainInfo* grain,
        uint8_t** payload);

    /**
     * Get grain info for a given index. This is used to inspect the grain info without opening the grain for mutation.
     *
     * \param[in] writer A valid flow writer
     * \param[in] index The index of the grain to obtain.
     * \param[out] mxlGrainInfo The requested mxlGrainInfo structure.
     * \return The result code. \see mxlStatus
     * \note Please note that this function can only be called on writers that
     *      operate on discrete flows. Any attempt to call this function on a
     *      writer that operates on another type of flow will result in an
     *      error.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterGetGrainInfo(mxlFlowWriter writer, uint64_t index, mxlGrainInfo* mxlGrainInfo);

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
     * that a new grain is available. The mxlGrainInfo flags field in shared memory will be updated based on grain->flags This will increase the head
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
     * \param[in] timeoutNs How long to wait in nanoseconds for the range of
     *      samples to become available.
     * \param[out] payloadBuffersSlices A pointer to a wrapped multi buffer
     *      slice that represents the requested range across all channel
     *      buffers.
     *
     * \return A status code describing the outcome of the call. Please note
     *      that this method will never return MXL_ERR_TIMEOUT, because the
     *      actual error that is being encountered in this case is
     *      MXL_ERR_OUT_OF_RANGE_TOO_EARLY, even after waiting.
     * \note No guarantees are made as to how long the caller may
     *      safely hang on to the returned range of samples without the
     *      risk of these samples being overwritten.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetSamples(mxlFlowReader reader, uint64_t index, size_t count, uint64_t timeoutNs,
        mxlWrappedMultiBufferSlice* payloadBuffersSlices);

    /**
     * Non-blocking accessor for a specific set of samples across all channels
     * ending at a specific index (`count` samples up to `index`).
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
    mxlStatus mxlFlowReaderGetSamplesNonBlocking(mxlFlowReader reader, uint64_t index, size_t count,
        mxlWrappedMultiBufferSlice* payloadBuffersSlices);

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

    /**
     * Create a new, empty synchronization group that can be used to synchronize on data availability across multiple flows in parallel.
     * \param[in] instance The instance to which the flow readers handled by this group belong.
     * \param[out] group A handle referring to the newly created group.
     *
     * \return MXL_STATUS_OK if the group was successfully created.
     */
    MXL_EXPORT
    mxlStatus mxlCreateFlowSynchronizationGroup(mxlInstance instance, mxlFlowSynchronizationGroup* group);

    /**
     * Release a synchronization group that is no longer needed and free all underlying resources associated with it.
     * \param[in] instance the instance on which the group was previously created.
     * \param[in] group A handle referring to the group that should be released.
     *
     * \return MXL_STATUS_OK if the group was successfully released.
     */
    MXL_EXPORT
    mxlStatus mxlReleaseFlowSynchronizationGroup(mxlInstance instance, mxlFlowSynchronizationGroup group);

    /**
     * Add a flow reader to a synchronization group. This can either be a reader
     * for a continuous, sample based flow or a discrete, grain based flow. For
     * sample based flows this causes the wait operation of the group to wait
     * for the sample belonging to the specified timestamp to become available.
     * For grain based flows this causes the wait operation of the group to wait
     * for the grain belonging to the specified timestamp to become fully
     * available.
     *
     * \param[in] group A handle to the synchronization group, to which the flow
     *      reader shall be added.
     * \param[in] reader A handle to a flow reader that shall be added to the
     *      synchronization group.
     *
     * \return MXL_STATUS_OK if the reader was successfully added to the
     *      synchronization group.
     *
     * \note Please note that a reader can only be added to a group once and
     *      attempts to add the same reader multiple times are ignored, except
     *      that for grain based readers the number of valid slices to wait for
     *      is updated to the value matching the last add operation.
     */
    MXL_EXPORT
    mxlStatus mxlFlowSynchronizationGroupAddReader(mxlFlowSynchronizationGroup group, mxlFlowReader reader);

    /**
     * Add a grain based flow reader to a synchronization group. This causes the
     * wait operation of the group to wait for at least \p minValidSlices of the
     * grain belonging to the specified timestamp to become.
     *
     * \param[in] group A handle to the synchronization group, to which the flow
     *      reader shall be added.
     * \param[in] reader A handle to a discrete flow reader that shall be added
     *      to the synchronization group.
     * \param[in] minValidSlices The number of valid slices to wait for within a
     *      grain of the flow the reader operates on.
     *
     * \return MXL_STATUS_OK if the reader was successfully added to the
     *      synchronization group.
     *
     * \note Please note that a reader can only be added to a group once and
     *      attempts to add the same reader multiple times are ignored, except
     *      that the number of valid slices to wait for is updated to the value
     *      matching the last add operation.
     */
    MXL_EXPORT
    mxlStatus mxlFlowSynchronizationGroupAddPartialGrainReader(mxlFlowSynchronizationGroup group, mxlFlowReader reader, uint16_t minValidSlices);

    /**
     * Remove a flow reader from a synchronization group.
     *
     * \param[in] group A handle to the synchronization group, from which the
     *      flow reader shall be removed.
     * \param[in] reader A handle to a flow reader that shall be removed from
     *      the synchronization group.
     *
     * \return MXL_STATUS_OK if the reader was successfully removed from the
     *      synchronization group.
     */
    MXL_EXPORT
    mxlStatus mxlFlowSynchronizationGroupRemoveReader(mxlFlowSynchronizationGroup group, mxlFlowReader reader);

    /**
     * Wait for the data corresponding to the specified timestamp to become
     * available accross all flows currently added to the group.
     *
     * \param[in] group A handle to the synchronization group that specifies the
     *      flows on which to wait for data.
     * \param[in] timestamp The timestamp for which to wait for corresponding
     *      data on each flow.
     * \param[in] timeoutNs How long to wait for all data to become available
     *      (in nanoseconds).
     *
     * \return MXL_STATUS_OK if the data corresponding to the specified
     *      timestamp has become available.
     */
    MXL_EXPORT
    mxlStatus mxlFlowSynchronizationGroupWaitForDataAt(mxlFlowSynchronizationGroup group, uint64_t timestamp, uint64_t timeoutNs);
#ifdef __cplusplus
}
#endif
