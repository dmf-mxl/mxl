#pragma once

#ifdef __cplusplus
#   include <cstdint>
#else
#   include <stdint.h>
#endif

#include <mxl/mxl.h>
#include <mxl/flowinfo.h>

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
    /// \param[out] info A pointer to a FlowInfo structure.  If not nullptr, this structure will be updated with the flow information after the flow is
    /// created.
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
    mxlStatus mxlFlowReaderGetGrain(mxlFlowReader reader, uint64_t index, uint64_t timeoutNs, GrainInfo* grain,
        uint8_t** payload);

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
    mxlStatus mxlFlowReaderGetGrainNonBlocking(mxlFlowReader reader, uint64_t index, GrainInfo* grain,
        uint8_t** payload);

    /**
     * Open a grain for mutation.  The flow writer will remember which index is currently opened. Before opening a new grain
     * for mutation, the user must either cancel the mutation using mxlFlowWriterCancelGrain or mxlFlowWriterCommitGrain.
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
    mxlStatus mxlFlowWriterOpenGrain(mxlFlowWriter writer, uint64_t index, GrainInfo* grainInfo,
        uint8_t** payload);

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

#ifdef __cplusplus
}
#endif