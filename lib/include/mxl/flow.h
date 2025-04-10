#pragma once

#include <mxl/mxl.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <time.h>

/**
 * Binary structure stored in the Flow shared memory segment.
 * The flow shared memory will be located in {mxlDomain}/{flowId}
 * where {mxlDomain} is a filesystem location available to the application
 */
typedef struct FlowInfo
{
    /// Version of the structure. The only currently supported value is 1
    uint32_t version;

    /// Size of the structure
    uint32_t size;

    /// The flow uuid.  This should be identical to the {flowId} path component described above.
    uint8_t id[16];

    /// The current head index of the ringbuffer
    uint64_t headIndex;

    /// The 'edit rate' of the grains expressed as a rational.  For VIDEO and ANC this value must match the editRate found in the flow descriptor.
    /// \todo Handle non-grain aligned sound access
    Rational grainRate;

    /// How many grains in the ring buffer. This should be identical to the number of entries in the {mxlDomain}/{flowId}/grains/ folder.
    /// Accessing the shared memory section for that specific grain should be predictable
    uint32_t grainCount;

    /// no flags defined yet.
    uint32_t flags;

    /// The last time a producer wrote to the flow
    struct timespec lastWriteTime;

    /// The last time a consumer read from the flow
    struct timespec lastReadTime;

    /// User data space
    uint8_t userData[4008];
} FlowInfo;

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
    /// Size of the grain payload.  This is the 'complete' size, when all slices are written.
    uint32_t grainSize;
    /// A video grain can have multiple slices.  This is the number of slices in the grain.
    int32_t sliceCount;
    /// The current number of slices written in the grain.
    int32_t validSliceCount;
    /// User data space
    uint8_t userData[4072];
} GrainInfo;

typedef struct GrainAccessor
{
    /// Pointer to the grain payload.
    /// - If GrainInfo.PayloadLocation is host memory grains, this points to the beginning of the grain data.
    /// - If GrainInfo.PayloadLocation is device memory, this point to a host buffer that contains a handle to device memory.
    uint8_t *payload;

    /// The total size of the grain (in bytes)
    uint32_t payloadSize;

    /// The current valid size (in bytes) of the grain (for sliced grains, this value will increase progressively based on the number of slices
    /// written)
    uint32_t validSize;

} GrainAccessor;

typedef struct mxlFlowReader_t *mxlFlowReader;
typedef struct mxlFlowWriter_t *mxlFlowWriter;

/**
 * Create a flow using a json flow definition
 *
 * \param in_instance The mxl instance created using mxlCreateInstance
 * \param in_flowDef A flow definition in the NMOS Flow json format.  The flow ID is read from the <id> field of this json object.
 * \param in_options Additional options (undefined). \todo Specify and used the additional options.
 * \param out_info A pointer to a FlowInfo structure.  If not nullptr, this structure will be updated with the flow information after the flow is
 * created.
 * \return \see mxlStatus for details.
 */
MXL_EXPORT mxlStatus mxlCreateFlow( mxlInstance in_instance, const char *in_flowDef, const char *in_options, FlowInfo *out_info );

/**
 * Destroy a flow. Will release all underlying resources
 *
 * \param in_instance The mxl instance created using mxlCreateInstance
 * \param in_flowDef The flow id.
 * \return \see mxlStatus for details.
 */
MXL_EXPORT mxlStatus mxlDestroyFlow( mxlInstance in_instance, const char *in_flowId );

/**
 * Create a flow reader for a specific flow id
 *
 * \param in_instance The mxl instance created using mxlCreateInstance
 * \param in_flowDef The id of an existing flow
 * \param in_options Additional options (undefined). \todo Specify and used the additional options.
 * \param out_reader A pointer to the newly created flow reader.
 * \return \see mxlStatus for details.
 */
MXL_EXPORT mxlStatus mxlCreateFlowReader( mxlInstance in_instance, const char *in_flowId, const char *in_options, mxlFlowReader *out_reader );

/**
 * Destroy a flow reader.
 *
 * \param in_instance The mxl instance created using mxlCreateInstance
 * \param in_reader The flowreader created with mxlCreateFlowReader
 * \return \see mxlStatus for details.
 */
MXL_EXPORT mxlStatus mxlDestroyFlowReader( mxlInstance in_instance, mxlFlowReader in_reader );

/**
 * Create a flow writer for a specific flow id
 *
 * \param in_instance The mxl instance created using mxlCreateInstance
 * \param in_flowDef The id of an existing flow
 * \param in_options Additional options (undefined). \todo Specify and used the additional options.
 * \param out_writer A pointer to the newly created flow writer.
 * \return \see mxlStatus for details.
 */
MXL_EXPORT mxlStatus mxlCreateFlowWriter( mxlInstance in_instance, const char *flowId, const char *in_options, mxlFlowWriter *out_writer );

/**
 * Destroy a flow writer.
 *
 * \param in_instance The mxl instance created using mxlCreateInstance
 * \param in_reader The flow writer created with mxlCreateFlowWriter
 * \return \see mxlStatus for details.
 */
MXL_EXPORT mxlStatus mxlDestroyFlowWriter( mxlInstance in_instance, mxlFlowWriter in_writer );

/**
 * Get the current head and tail values of a Flow
 *
 * \param in_instance A valid mxl instance
 * \param in_reader A valid flow reader
 * \param out_info A valid pointer to a FlowInfo structure. on return, the structure will be updated with a copy of the current flow info value.
 * \return The result code. \see mxlStatus
 */
MXL_EXPORT mxlStatus mxlFlowReaderGetInfo( mxlInstance in_instance, mxlFlowReader in_reader, FlowInfo *out_info );

/**
 * Accessor for a flow grain at a specific index
 *
 * \param in_instance A valid mxl instance
 * \param in_reader A valid flow reader
 * \param in_index The index of the grain to obtain
 * \param in_timeoutMs How long should we wait for the grain (in milliseconds)
 * \param out_grain The requested GrainInfo structure.
 * \param out_accessor The requested grain accessor. Will contain pointers to payload and size/valid size information.
 *
 * \note The returned grain may be partial. This function will return when a slice is updated.  Valid bytes declared in the out_accessor field.
 * \return The result code. \see mxlStatus
 */
MXL_EXPORT mxlStatus mxlFlowReaderGetGrain(
    mxlInstance in_instance, mxlFlowReader in_reader, uint64_t in_index, uint16_t in_timeoutMs, GrainInfo *out_grain, GrainAccessor *out_accessor );

/**
 * Open a grain for mutation.  The flow writer will remember which index is currently opened. Before opening a new grain
 * for mutation, the user must either cancel the mutation using mxlFlowWriterCancel or mxlFlowWriterCommit
 *
 * \param in_instance A valid mxl instance
 * \param in_writer A valid flow writer
 * \param in_index The index of the grain to obtain
 * \param out_grainInfo The requested GrainInfo structure.
 * \param out_payload The requested grain payload.
 * \return The result code. \see mxlStatus
 */
MXL_EXPORT mxlStatus
mxlFlowWriterOpenGrain( mxlInstance in_instance, mxlFlowWriter in_writer, uint64_t in_index, GrainInfo *out_grainInfo, uint8_t **out_payload );

/**
 * Increment the current slice index of the grain.
 * This should be used when writing multi-slice grains (GrainInfo::sliceCount > 1).
 * FlowReaders waiting on a grain will be notified after the slice is incremented.
 * This function is only relevent when writing multi-slice grains.
 *
 * \param in_instance A valid mxl instance
 * \param in_writer A valid flow writer
 * \param out_grainInfo A copy of the updated GrainInfo structure for the currently opened grain.
 * \return The result code. \see mxlStatus
 * \note This function should be called after writing the grain data for the current slice.
 */
MXL_EXPORT mxlStatus mxlFlowWriterIncrementSlice( mxlInstance in_instance, mxlFlowWriter in_writer, GrainInfo *out_grainInfo );

/**
 * Cancel the current grain mutation.  This will reset the flow writer current grain index
 *
 * \param in_instance A valid mxl instance
 * \param in_writer A valid flow writer
 * \return The result code. \see mxlStatus
 */
MXL_EXPORT mxlStatus mxlFlowWriterCancel( mxlInstance in_instance, mxlFlowWriter in_writer );

/**
 * Inform mxl that a user is done writing the grain that was previously opened.  This will in turn signal all readers waiting on the ringbuffer that
 * a new grain is available.  The graininfo flags field in shared memory will be updated based on in_grain->flags
 * This will increase the head and potentially the tail IF this grain is the new head.
 * If the grain is multi-slice, this will set the sliceIndex = sliceCount-1 (mark it as complete)s
 *
 * \param in_instance A valid mxl instance
 * \param in_writer A valid flow writer
 * \param in_grainInfo Details about the grain being committed.
 * \return The result code. \see mxlStatus
 */
MXL_EXPORT mxlStatus mxlFlowWriterCommit( mxlInstance in_instance, mxlFlowWriter in_writer, const GrainInfo *in_grainInfo );

#ifdef __cplusplus
}
#endif