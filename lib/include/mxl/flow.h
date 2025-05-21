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
    /// \param in_instance The mxl instance created using mxlCreateInstance
    /// \param in_flowDef A flow definition in the NMOS Flow json format.  The flow ID is read from the <id> field of this json object.
    /// \param in_options Additional options (undefined). \todo Specify and used the additional options.
    /// \param out_info A pointer to a FlowInfo structure.  If not nullptr, this structure will be updated with the flow information after the flow is
    /// created.
MXL_EXPORT
    mxlStatus mxlCreateFlow(mxlInstance in_instance, char const* in_flowDef, char const* in_options, FlowInfo* out_info);

    MXL_EXPORT
    mxlStatus mxlDestroyFlow(mxlInstance in_instance, char const* in_flowId);

    MXL_EXPORT
    mxlStatus mxlCreateFlowReader(mxlInstance in_instance, char const* in_flowId, char const* in_options, mxlFlowReader* out_reader);

    MXL_EXPORT
    mxlStatus mxlReleaseFlowReader(mxlInstance in_instance, mxlFlowReader in_reader);

    MXL_EXPORT
    mxlStatus mxlCreateFlowWriter(mxlInstance in_instance, char const* flowId, char const* in_options, mxlFlowWriter* out_writer);

    MXL_EXPORT
    mxlStatus mxlReleaseFlowWriter(mxlInstance in_instance, mxlFlowWriter in_writer);

    /**
     * Get the current head and tail values of a Flow
     *
     * \param in_reader A valid flow reader
     * \param out_info A valid pointer to a FlowInfo structure. on return, the structure will be updated with a copy of the current flow info value.
     * \return The result code. \see mxlStatus
     */
MXL_EXPORT
    mxlStatus mxlFlowReaderGetInfo(mxlFlowReader in_reader, FlowInfo* out_info);

    /**
     * Accessor for a flow grain at a specific index
     *
     * \param in_reader A valid flow reader
     * \param in_index The index of the grain to obtain
     * \param in_timeoutNs How long should we wait for the grain (in nanoseconds)
     * \param out_grain The requested GrainInfo structure.
     * \param out_payload The requested grain payload.
     * \return The result code. \see mxlStatus
     */
MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrain(mxlFlowReader in_reader, uint64_t in_index, uint64_t in_timeoutNs, GrainInfo* out_grain,
        uint8_t** out_payload);

    /**
     * Non-blocking accessor for a flow grain at a specific index
     *
     * \param in_reader A valid flow reader
     * \param in_index The index of the grain to obtain
     * \param out_grain The requested GrainInfo structure.
     * \param out_payload The requested grain payload.
     * \return The result code. \see mxlStatus
     */
MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrainNonBlocking(mxlFlowReader in_reader, uint64_t in_index, GrainInfo* out_grain,
        uint8_t** out_payload);

    /**
     * Open a grain for mutation.  The flow writer will remember which index is currently opened. Before opening a new grain
     * for mutation, the user must either cancel the mutation using mxlFlowWriterCancel or mxlFlowWriterCommit
     *
     * \param in_writer A valid flow writer
     * \param in_index The index of the grain to obtain
     * \param out_grainInfo The requested GrainInfo structure.
     * \param out_payload The requested grain payload.
     * \return The result code. \see mxlStatus
     */
MXL_EXPORT
    mxlStatus mxlFlowWriterOpenGrain(mxlFlowWriter in_writer, uint64_t in_index, GrainInfo* out_grainInfo,
        uint8_t** out_payload);

    /**
     *
     * \param in_writer A valid flow writer
     */
MXL_EXPORT
    mxlStatus mxlFlowWriterCancel(mxlFlowWriter in_writer);

    /**
     * Inform mxl that a user is done writing the grain that was previously opened.  This will in turn signal all readers waiting on the ringbuffer
     * that a new grain is available.  The graininfo flags field in shared memory will be updated based on in_grain->flags This will increase the head
     * and potentially the tail IF this grain is the new head.
     *
     * \return The result code. \see mxlStatus
     */
MXL_EXPORT
    mxlStatus mxlFlowWriterCommit(mxlFlowWriter in_writer, GrainInfo const* in_grain);

#ifdef __cplusplus
}
#endif