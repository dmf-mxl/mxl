// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file flow.h
 * @brief The primary MXL Flow API -- readers, writers, grains, samples, and synchronization.
 *
 * This is the main header for applications that exchange media through MXL.
 * It provides:
 *
 *   - **Flow Writers**  -- produce media (video frames, audio samples, data)
 *   - **Flow Readers**  -- consume media (zero-copy shared-memory access)
 *   - **Discrete Grain API** -- for VIDEO and DATA flows (one grain at a time)
 *   - **Continuous Sample API** -- for AUDIO flows (arbitrary windows of samples)
 *   - **Synchronization Groups** -- wait for data across multiple flows simultaneously
 *   - **Buffer-slice helpers** -- describe ring-buffer regions that may wrap around
 *
 * ## Quick start (discrete video flow)
 * @code
 *     // 1. Create a writer
 *     mxlFlowWriter writer;
 *     mxlCreateFlowWriter(instance, flowDefJson, NULL, &writer, NULL, NULL);
 *
 *     // 2. Open a grain for writing
 *     mxlGrainInfo grain;
 *     uint8_t* payload;
 *     mxlFlowWriterOpenGrain(writer, grainIndex, &grain, &payload);
 *
 *     // 3. Fill the payload buffer with frame data ...
 *     memcpy(payload, frameData, grain.grainSize);
 *
 *     // 4. Commit -- this wakes up any waiting readers
 *     grain.validSlices = grain.totalSlices;
 *     mxlFlowWriterCommitGrain(writer, &grain);
 * @endcode
 *
 * ## Quick start (continuous audio flow)
 * @code
 *     mxlMutableWrappedMultiBufferSlice slices;
 *     mxlFlowWriterOpenSamples(writer, nextSampleIndex, batchSize, &slices);
 *     // Fill slices for every channel (respecting fragments and stride) ...
 *     mxlFlowWriterCommitSamples(writer);
 * @endcode
 */

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

/* =========================================================================
 * Grain flags and slice constants
 * ========================================================================= */

/**
 * Flag: this grain does not contain valid media data.
 *
 * A writer sets this flag (via mxlGrainInfo.flags) when it cannot produce
 * a valid grain in time -- for example, if the upstream source timed out.
 * The grain still advances the ring buffer head so that downstream readers
 * stay in sync, but readers should treat the payload as invalid and
 * substitute a fallback (repeat previous frame, insert silence, etc.).
 */
#define MXL_GRAIN_FLAG_INVALID 0x00000001 /* bit 0 */

/**
 * Pass as `minValidSlices` to accept a grain with any number of committed
 * slices, including zero.  Useful when polling for partial availability.
 */
#define MXL_GRAIN_VALID_SLICES_ANY ((uint16_t)0)

/**
 * Pass as `minValidSlices` to require all slices to be committed before
 * the read returns.  This is the "wait for the full grain" mode.
 */
#define MXL_GRAIN_VALID_SLICES_ALL ((uint16_t)UINT16_MAX)

    /* =====================================================================
     * Buffer-slice helper types
     * =====================================================================
     * Because ring buffers wrap around, a contiguous logical range of bytes
     * may map to TWO separate physical memory regions.  The "slice" types
     * below represent these zero, one, or two fragments.
     *
     * For multi-channel continuous flows the pattern repeats once per
     * channel, separated by a fixed `stride` in bytes.
     *
     * Const variants  -> returned by reader functions (read-only access)
     * Mutable variants -> returned by writer functions (read-write access)
     * =================================================================== */

    /**
     * A contiguous, read-only region of bytes in memory.
     * Used as a building block for the wrapped-buffer types below.
     */
    typedef struct mxlBufferSlice_t
    {
        void const* pointer; /**< Start of the byte range.            */
        size_t      size;    /**< Number of bytes in this fragment.   */
    } mxlBufferSlice;

    /**
     * A contiguous, writable region of bytes in memory.
     */
    typedef struct mxlMutableBufferSlice_t
    {
        void*  pointer; /**< Start of the byte range (writable). */
        size_t size;    /**< Number of bytes in this fragment.   */
    } mxlMutableBufferSlice;

    /**
     * A logical range of bytes within a ring buffer, possibly split across
     * the wrap-around point into two physical fragments.
     *
     * - `fragments[0]` is always valid (may have size 0 if the range is empty).
     * - `fragments[1]` is non-empty only when the range wraps around the end
     *   of the ring buffer and continues from the beginning.
     *
     * Example (ring buffer of 100 bytes, reading bytes 90..109):
     *   fragments[0] = { ptr=base+90, size=10 }  // bytes 90..99
     *   fragments[1] = { ptr=base+0,  size=10 }  // bytes 0..9  (wrapped)
     */
    typedef struct mxlWrappedBufferSlice_t
    {
        mxlBufferSlice fragments[2]; /**< Up to two fragments for the wrapped range. */
    } mxlWrappedBufferSlice;

    /**
     * Mutable version of mxlWrappedBufferSlice -- for writer access.
     */
    typedef struct mxlMutableWrappedBufferSlice_t
    {
        mxlMutableBufferSlice fragments[2]; /**< Up to two writable fragments. */
    } mxlMutableWrappedBufferSlice;

    /**
     * A wrapped buffer slice replicated across multiple ring buffers
     * (one per audio channel), separated by a fixed stride.
     *
     * For an N-channel audio flow:
     *   - `base` describes the fragments for channel 0.
     *   - Channel C's fragment pointers are at `base.fragments[i].pointer + C * stride`.
     *   - `count` == N (total number of channels).
     *
     * For discrete (video/data) flows, `count` is 1 and `stride` is 0.
     */
    typedef struct mxlWrappedMultiBufferSlice_t
    {
        mxlWrappedBufferSlice base; /**< Fragment geometry for the first channel/buffer. */

        /**
         * Byte distance between the same offset in consecutive channels.
         * Add `stride` to a channel-0 pointer to get the channel-1 pointer, etc.
         */
        size_t stride;

        /**
         * Total number of ring buffers (channels) in the sequence.
         */
        size_t count;
    } mxlWrappedMultiBufferSlice;

    /**
     * Mutable version of mxlWrappedMultiBufferSlice -- for writer access.
     */
    typedef struct mxlMutableWrappedMultiBufferSlice_t
    {
        mxlMutableWrappedBufferSlice base; /**< Writable fragment geometry for channel 0. */

        size_t stride; /**< Byte distance between consecutive channel ring buffers. */
        size_t count;  /**< Total number of channels / ring buffers.               */
    } mxlMutableWrappedMultiBufferSlice;

    /* =====================================================================
     * Grain metadata
     * =====================================================================
     * This 4096-byte structure is stored at the beginning of every grain
     * file in `${mxlDomain}/${flowId}.mxl-flow/grains/${ringIndex}`.  It
     * is followed immediately by the grain's payload bytes.
     *
     * The structure supports "partial writes" -- a writer can commit
     * individual slices (e.g. scan lines) progressively, incrementing
     * validSlices each time, while readers observe the latest committed
     * slice count.
     * =================================================================== */

    /**
     * Per-grain metadata header.
     *
     * Stored at the start of each grain's shared-memory file, immediately
     * followed by the payload bytes.  Both writers (read-write) and readers
     * (read-only) memory-map this structure.
     *
     * Total size: 4096 bytes (including reserved padding) for alignment.
     */
    typedef struct mxlGrainInfo_t
    {
        /**
         * Structure version number.  Currently must be 2.
         * Readers should reject values they do not understand.
         */
        uint32_t version;

        /**
         * Total size of this structure in bytes (always 4096).
         */
        uint32_t size;

        /**
         * Absolute grain index since the epoch (SMPTE ST 2059).
         *
         * This is the "epoch grain index" -- it increases monotonically
         * across the lifetime of the flow and wraps around the ring buffer
         * via: ringBufferIndex = index % grainCount.
         */
        uint64_t index;

        /**
         * Bitfield of grain-level flags.
         * @see MXL_GRAIN_FLAG_INVALID
         */
        uint32_t flags;

        /**
         * Total size in bytes of the complete grain payload (all slices
         * across all planes combined).
         */
        uint32_t grainSize;

        /**
         * Total number of slices that make up a complete grain.
         *
         * A "slice" is the smallest unit that can be independently committed:
         *   - For VIDEO: one scan line of pixels in the payload format.
         *   - For DATA:  a single byte.
         *
         * A grain is fully written when validSlices == totalSlices.
         */
        uint16_t totalSlices;

        /**
         * Number of slices committed so far.
         *
         * Updated by the writer on each mxlFlowWriterCommitGrain() call.
         * Readers should always check this before processing the payload:
         *   - validSlices == totalSlices  -> grain is complete
         *   - validSlices <  totalSlices  -> grain is partially written
         *   - validSlices == 0            -> grain has been opened but nothing committed yet
         */
        uint16_t validSlices;

        /**
         * Reserved padding to bring the total structure to 4096 bytes.
         * Must be zero-filled.  Do not read or write.
         */
        uint8_t reserved[4068];
    } mxlGrainInfo;

    /* =====================================================================
     * Opaque handles
     * =====================================================================
     * Readers and writers are represented as opaque pointers.  The actual
     * types live inside the library; callers only hold handles.
     * =================================================================== */

    /** Opaque handle to a flow reader (consumer side).  Created by mxlCreateFlowReader(). */
    typedef struct mxlFlowReader_t* mxlFlowReader;

    /** Opaque handle to a flow writer (producer side).  Created by mxlCreateFlowWriter(). */
    typedef struct mxlFlowWriter_t* mxlFlowWriter;

    /** Opaque handle to a synchronization group.  Created by mxlCreateFlowSynchronizationGroup(). */
    typedef struct mxlFlowSynchronizationGroup_t* mxlFlowSynchronizationGroup;

    /* =====================================================================
     * Writer lifecycle
     * =====================================================================
     * A writer creates (or opens an existing) flow in the domain and
     * obtains exclusive write access to its shared-memory ring buffer.
     * =================================================================== */

    /**
     * Create (or open) a flow writer.
     *
     * If no flow with the UUID specified in \p flowDef exists in the domain,
     * a new flow is created (shared-memory files are allocated, the JSON
     * definition is written to `flow_def.json`, etc.) and \p *created is
     * set to `true`.
     *
     * If a flow with the same UUID already exists, it is opened for writing
     * and \p *created is set to `false`.  Note: the existing flow's definition
     * may differ from \p flowDef -- use mxlGetFlowDef() to retrieve the
     * actual definition.
     *
     * @param[in]  instance    A valid MXL instance.
     * @param[in]  flowDef     NMOS IS-04 Flow Resource JSON string describing
     *                         the desired flow.  Must contain a valid `id` field.
     * @param[in]  options     Reserved for future use.  Pass NULL.
     * @param[out] writer      Receives the newly created writer handle.
     * @param[out] configInfo  (optional) If non-NULL, filled with the flow's
     *                         immutable configuration after creation/opening.
     * @param[out] created     (optional) If non-NULL, set to `true` when a
     *                         new flow was created, `false` when an existing
     *                         flow was opened.
     * @return MXL_STATUS_OK on success, or an appropriate error code.
     */
    MXL_EXPORT
    mxlStatus mxlCreateFlowWriter(mxlInstance instance, char const* flowDef, char const* options, mxlFlowWriter* writer,
        mxlFlowConfigInfo* configInfo, bool* created);

    /**
     * Release a flow writer and free all associated resources.
     *
     * The writer's shared advisory lock on the flow's `data` file is
     * released, allowing garbage collection to detect that the flow is
     * no longer actively written.
     *
     * @param[in] instance  The MXL instance that owns the writer.
     * @param[in] writer    The writer handle to release.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlReleaseFlowWriter(mxlInstance instance, mxlFlowWriter writer);

    /* =====================================================================
     * Reader lifecycle
     * =====================================================================
     * A reader opens an existing flow by its UUID and memory-maps the
     * shared-memory files in read-only mode for zero-copy access.
     * =================================================================== */

    /**
     * Create a flow reader for an existing flow in the domain.
     *
     * The reader memory-maps the flow's `data` file (header) and grain/
     * channel files in **read-only** mode (PROT_READ), so it can operate
     * even on read-only volumes.
     *
     * @param[in]  instance  A valid MXL instance.
     * @param[in]  flowId    UUID string (lowercase hex with hyphens) of the
     *                       flow to read.
     * @param[in]  options   Reserved for future use.  Pass NULL.
     * @param[out] reader    Receives the newly created reader handle.
     * @return MXL_STATUS_OK on success,
     *         MXL_ERR_FLOW_NOT_FOUND if no flow with this ID exists.
     */
    MXL_EXPORT
    mxlStatus mxlCreateFlowReader(mxlInstance instance, char const* flowId, char const* options, mxlFlowReader* reader);

    /**
     * Release a flow reader and unmap its shared-memory regions.
     *
     * @param[in] instance  The MXL instance that owns the reader.
     * @param[in] reader    The reader handle to release.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlReleaseFlowReader(mxlInstance instance, mxlFlowReader reader);

    /* =====================================================================
     * Flow introspection
     * =================================================================== */

    /**
     * Check whether a flow currently has an active writer.
     *
     * A flow is "active" if some process holds a shared advisory file lock
     * on its `data` file.  This is a non-blocking check.
     *
     * @param[in]  instance   A valid MXL instance.
     * @param[in]  flowId     UUID of the flow to check.
     * @param[out] isActive   Set to `true` if an active writer exists.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlIsFlowActive(mxlInstance instance, char const* flowId, bool* isActive);

    /**
     * Retrieve the NMOS IS-04 JSON flow definition for a given flow.
     *
     * The JSON string is read from the flow's `flow_def.json` file on disk.
     * If the supplied buffer is too small, the function returns
     * MXL_ERR_INVALID_ARG and writes the *required* buffer size (including
     * the null terminator) into \p *bufferSize, allowing the caller to
     * retry with a larger buffer.
     *
     * @param[in]     instance    A valid MXL instance.
     * @param[in]     flowId      UUID of the flow.
     * @param[out]    buffer      Destination buffer for the JSON string.
     *                            May be NULL to query the required size.
     * @param[in,out] bufferSize  On entry: size of \p buffer in bytes.
     *                            On exit : bytes written (including '\\0')
     *                            or required size if the buffer was too small.
     * @return MXL_STATUS_OK on success,
     *         MXL_ERR_INVALID_ARG on NULL pointers or insufficient buffer.
     */
    MXL_EXPORT
    mxlStatus mxlGetFlowDef(mxlInstance instance, char const* flowId, char* buffer, size_t* bufferSize);

    /* =====================================================================
     * Reader -- flow info accessors
     * =====================================================================
     * These functions copy the flow's shared-memory header (or parts of it)
     * into a caller-owned structure.  The copy is a snapshot; the writer
     * may update the runtime info immediately after.
     * =================================================================== */

    /**
     * Get a snapshot of the **full** flow header (config + runtime).
     *
     * This is a convenience function equivalent to reading both the
     * immutable config info and the mutable runtime info at once.
     *
     * @param[in]  reader  A valid flow reader handle.
     * @param[out] info    Filled with a copy of the 2048-byte flow header.
     * @return MXL_STATUS_OK on success.
     * @see mxlFlowReaderGetConfigInfo, mxlFlowReaderGetRuntimeInfo
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetInfo(mxlFlowReader reader, mxlFlowInfo* info);

    /**
     * Get a snapshot of the **immutable** flow configuration.
     *
     * The returned data never changes after the flow is created, so it is
     * safe to cache.  Contains the UUID, data format, grain rate, ring-
     * buffer geometry, payload location, etc.
     *
     * @param[in]  reader  A valid flow reader handle.
     * @param[out] info    Filled with the immutable config info.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetConfigInfo(mxlFlowReader reader, mxlFlowConfigInfo* info);

    /**
     * Get a snapshot of the **mutable** runtime state of the flow.
     *
     * Returns the current head index, last-write time, and last-read time.
     * This is the information that changes as the writer produces new data.
     *
     * @param[in]  reader  A valid flow reader handle.
     * @param[out] info    Filled with the current runtime info.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetRuntimeInfo(mxlFlowReader reader, mxlFlowRuntimeInfo* info);

    /* =====================================================================
     * Reader -- Discrete Grain API (VIDEO / DATA flows only)
     * =====================================================================
     * These functions provide zero-copy read access to individual grains in
     * a discrete flow's ring buffer.  The returned `payload` pointer points
     * directly into shared memory -- no copy is made.
     *
     * Blocking variants wait (via futex) until the grain becomes available
     * or the timeout expires.  Non-blocking variants return immediately
     * with MXL_ERR_OUT_OF_RANGE_TOO_EARLY if the grain is not yet written.
     *
     * IMPORTANT: All functions in this section are for DISCRETE flows only.
     *            Calling them on a continuous (audio) flow reader will
     *            return an error.
     * =================================================================== */

    /**
     * Read a **complete** grain at the specified index, blocking until it
     * is fully committed or the timeout expires.
     *
     * "Complete" means all slices are valid (validSlices == totalSlices).
     * For partial-grain access, use mxlFlowReaderGetGrainSlice() instead.
     *
     * The returned \p *payload pointer points directly into the memory-mapped
     * grain file and remains valid until the grain's ring-buffer slot is
     * overwritten by a future write.
     *
     * @param[in]  reader     A valid **discrete** flow reader.
     * @param[in]  index      Absolute grain index (epoch-based).
     * @param[in]  timeoutNs  Maximum wait time in nanoseconds.
     * @param[out] grain      Filled with the grain's metadata header.
     * @param[out] payload    Set to point to the grain's payload bytes.
     * @return MXL_STATUS_OK on success,
     *         MXL_ERR_OUT_OF_RANGE_TOO_LATE  if the grain has been overwritten,
     *         MXL_ERR_OUT_OF_RANGE_TOO_EARLY if the grain was not written in time,
     *         MXL_ERR_TIMEOUT on timeout.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrain(mxlFlowReader reader, uint64_t index, uint64_t timeoutNs, mxlGrainInfo* grain, uint8_t** payload);

    /**
     * Read a grain at the specified index, blocking until at least
     * \p minValidSlices slices have been committed.
     *
     * This enables "low-latency" reads where the reader starts processing
     * a grain before the writer has finished all slices (e.g., reading
     * the first N scan lines of a video frame while the rest is still
     * being written).
     *
     * @param[in]  reader          A valid discrete flow reader.
     * @param[in]  index           Absolute grain index.
     * @param[in]  minValidSlices  Minimum number of committed slices to wait for.
     *                             Use MXL_GRAIN_VALID_SLICES_ANY (0) for any,
     *                             MXL_GRAIN_VALID_SLICES_ALL for all.
     * @param[in]  timeoutNs       Maximum wait time in nanoseconds.
     * @param[out] grain           Filled with the grain's metadata header.
     * @param[out] payload         Set to point to the grain's payload bytes.
     * @return MXL_STATUS_OK when at least \p minValidSlices are available.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrainSlice(mxlFlowReader reader, uint64_t index, uint16_t minValidSlices, uint64_t timeoutNs, mxlGrainInfo* grain,
        uint8_t** payload);

    /**
     * Non-blocking read of a complete grain at the specified index.
     *
     * Returns immediately.  If the grain is not yet fully written,
     * returns MXL_ERR_OUT_OF_RANGE_TOO_EARLY.
     *
     * @param[in]  reader   A valid discrete flow reader.
     * @param[in]  index    Absolute grain index.
     * @param[out] grain    Filled with the grain's metadata header.
     * @param[out] payload  Set to point to the grain's payload bytes.
     * @return MXL_STATUS_OK if the grain is complete and available.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrainNonBlocking(mxlFlowReader reader, uint64_t index, mxlGrainInfo* grain, uint8_t** payload);

    /**
     * Non-blocking read of a grain with a minimum slice requirement.
     *
     * Combines the partial-slice semantics of mxlFlowReaderGetGrainSlice()
     * with non-blocking behaviour.  Returns immediately regardless of how
     * many slices have been committed.
     *
     * @param[in]  reader          A valid discrete flow reader.
     * @param[in]  index           Absolute grain index.
     * @param[in]  minValidSlices  Minimum committed slices required.
     * @param[out] grain           Filled with the grain's metadata header.
     * @param[out] payload         Set to point to the grain's payload bytes.
     * @return MXL_STATUS_OK if enough slices are available.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetGrainSliceNonBlocking(mxlFlowReader reader, uint64_t index, uint16_t minValidSlices, mxlGrainInfo* grain,
        uint8_t** payload);

    /* =====================================================================
     * Writer -- Discrete Grain API (VIDEO / DATA flows only)
     * =====================================================================
     * Writers produce grains in a two-phase protocol:
     *   1. OpenGrain  -> get a writable buffer and grain metadata
     *   2. CommitGrain / CancelGrain -> publish or discard the grain
     *
     * Only ONE grain may be open at a time per writer.  A grain must be
     * committed or cancelled before the next one can be opened.
     *
     * Partial writes are supported: the writer can commit slices
     * progressively by updating grain->validSlices and calling
     * CommitGrain multiple times for the same grain.
     * =================================================================== */

    /**
     * Inspect the metadata header of a grain WITHOUT opening it for writing.
     *
     * Useful for checking whether a particular grain slot has already been
     * written, its current slice count, flags, etc.
     *
     * @param[in]  writer       A valid discrete flow writer.
     * @param[in]  index        Absolute grain index.
     * @param[out] mxlGrainInfo Filled with the grain's metadata.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterGetGrainInfo(mxlFlowWriter writer, uint64_t index, mxlGrainInfo* mxlGrainInfo);

    /**
     * Open a grain slot for writing (mutation).
     *
     * The writer remembers which grain is currently open.  Before opening
     * another grain, the caller **must** either commit (mxlFlowWriterCommitGrain)
     * or cancel (mxlFlowWriterCancelGrain) the current one.
     *
     * The returned \p *payload pointer points into writable shared memory.
     * The caller fills this buffer with media data, then calls CommitGrain.
     *
     * @param[in]  writer       A valid discrete flow writer.
     * @param[in]  index        Absolute grain index to write to.
     * @param[out] mxlGrainInfo Filled with the grain's pre-existing metadata
     *                          (size, totalSlices, etc.).
     * @param[out] payload      Set to point to the writable payload buffer.
     * @return MXL_STATUS_OK on success.
     *
     * @todo Allow multiple grains open simultaneously via a per-grain handle.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterOpenGrain(mxlFlowWriter writer, uint64_t index, mxlGrainInfo* mxlGrainInfo, uint8_t** payload);

    /**
     * Cancel the currently open grain -- discard all data written to it.
     *
     * The grain's ring-buffer slot is left unchanged; no readers are notified.
     *
     * @param[in] writer  A valid discrete flow writer with an open grain.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterCancelGrain(mxlFlowWriter writer);

    /**
     * Commit the currently open grain -- publish it to readers.
     *
     * This does three things:
     *   1. Copies grain->flags into the shared-memory grain header
     *      (this is how MXL_GRAIN_FLAG_INVALID gets propagated).
     *   2. Updates the flow's runtime headIndex if this grain is the
     *      new head of the ring buffer.
     *   3. Signals all readers waiting on the ring buffer (via futex)
     *      that new data is available.
     *
     * Partial-grain workflow: the writer can call CommitGrain multiple
     * times for the same open grain, incrementing grain->validSlices
     * each time.  Each commit signals readers, allowing them to start
     * processing as soon as enough slices are available.
     *
     * @param[in] writer  A valid discrete flow writer with an open grain.
     * @param[in] grain   Grain metadata with updated flags / validSlices.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterCommitGrain(mxlFlowWriter writer, mxlGrainInfo const* grain);

    /* =====================================================================
     * Reader -- Continuous Sample API (AUDIO flows only)
     * =====================================================================
     * These functions provide zero-copy read access to windows of audio
     * samples across all channels in a continuous flow's ring buffer.
     *
     * The returned mxlWrappedMultiBufferSlice describes the region:
     *   - base.fragments[0/1] : up to two contiguous byte ranges
     *     (the range may straddle the ring-buffer wrap point)
     *   - stride : byte distance between the same offset in consecutive
     *     channels
     *   - count  : number of channels
     *
     * The caller iterates channels by adding (channel * stride) to each
     * fragment pointer.
     *
     * IMPORTANT: readers may only request up to bufferLength / 2 samples
     *            at a time (the other half is the "write zone").
     * =================================================================== */

    /**
     * Read a window of samples, blocking until the data is available.
     *
     * Reads `count` samples per channel **ending at** `index` (inclusive).
     * That is, samples [index - count + 1 .. index] are returned.
     *
     * @param[in]  reader               A valid **continuous** flow reader.
     * @param[in]  index                The last (most recent) sample index.
     * @param[in]  count                Number of samples to read per channel.
     *                                  Must be <= bufferLength / 2.
     * @param[in]  timeoutNs            Maximum wait time in nanoseconds.
     * @param[out] payloadBuffersSlices Filled with pointers into shared memory.
     * @return MXL_STATUS_OK on success.
     *         This function never returns MXL_ERR_TIMEOUT -- if the data is
     *         not available after waiting, it returns MXL_ERR_OUT_OF_RANGE_TOO_EARLY.
     *
     * @warning The returned pointers may become stale once the writer
     *          advances far enough to overwrite the requested region.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetSamples(mxlFlowReader reader, uint64_t index, size_t count, uint64_t timeoutNs,
        mxlWrappedMultiBufferSlice* payloadBuffersSlices);

    /**
     * Non-blocking version of mxlFlowReaderGetSamples().
     *
     * Returns immediately.  If the requested samples have not been written
     * yet, returns MXL_ERR_OUT_OF_RANGE_TOO_EARLY.
     *
     * @param[in]  reader               A valid continuous flow reader.
     * @param[in]  index                The last sample index in the window.
     * @param[in]  count                Number of samples per channel.
     * @param[out] payloadBuffersSlices Filled with pointers into shared memory.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowReaderGetSamplesNonBlocking(mxlFlowReader reader, uint64_t index, size_t count,
        mxlWrappedMultiBufferSlice* payloadBuffersSlices);

    /* =====================================================================
     * Writer -- Continuous Sample API (AUDIO flows only)
     * =====================================================================
     * Mirror of the reader API: open a range of samples for writing,
     * fill the buffers, then commit.  Only one range may be open at a time.
     * =================================================================== */

    /**
     * Open a range of samples across all channels for writing.
     *
     * The returned mxlMutableWrappedMultiBufferSlice describes writable
     * regions in shared memory.  The caller fills these buffers with audio
     * data, then calls mxlFlowWriterCommitSamples().
     *
     * @param[in]  writer               A valid continuous flow writer.
     * @param[in]  index                Starting sample index for this batch.
     * @param[in]  count                Number of samples per channel to write.
     * @param[out] payloadBuffersSlices Filled with writable pointers.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterOpenSamples(mxlFlowWriter writer, uint64_t index, size_t count, mxlMutableWrappedMultiBufferSlice* payloadBuffersSlices);

    /**
     * Cancel the currently open sample range -- discard all data.
     *
     * @param[in] writer  A valid continuous flow writer with an open range.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterCancelSamples(mxlFlowWriter writer);

    /**
     * Commit the currently open sample range -- publish to readers.
     *
     * Advances the flow's headIndex and signals waiting readers via futex.
     *
     * @param[in] writer  A valid continuous flow writer with an open range.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowWriterCommitSamples(mxlFlowWriter writer);

    /* =====================================================================
     * Flow Synchronization Groups
     * =====================================================================
     * A synchronization group allows a media function to wait for data
     * to become available across MULTIPLE flows simultaneously.
     *
     * Typical use case: a video mixer that consumes one video flow and
     * one audio flow and needs to wait until both have data at the same
     * timestamp before starting to process.
     *
     * Usage:
     *   1. mxlCreateFlowSynchronizationGroup()
     *   2. mxlFlowSynchronizationGroupAddReader() for each flow
     *   3. mxlFlowSynchronizationGroupWaitForDataAt() in the processing loop
     *   4. mxlReleaseFlowSynchronizationGroup() when done
     * =================================================================== */

    /**
     * Create a new, empty synchronization group.
     *
     * @param[in]  instance  The MXL instance that owns the readers to synchronize.
     * @param[out] group     Receives the newly created group handle.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlCreateFlowSynchronizationGroup(mxlInstance instance, mxlFlowSynchronizationGroup* group);

    /**
     * Release a synchronization group and free all associated resources.
     *
     * Does NOT release the individual readers that were added to the group.
     *
     * @param[in] instance  The MXL instance.
     * @param[in] group     The group handle to release.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlReleaseFlowSynchronizationGroup(mxlInstance instance, mxlFlowSynchronizationGroup group);

    /**
     * Add a flow reader to the synchronization group.
     *
     * Works for both continuous (audio) and discrete (video/data) readers:
     *   - **Continuous readers**: the group waits for the sample corresponding
     *     to the requested timestamp to become available.
     *   - **Discrete readers**: the group waits for the grain corresponding
     *     to the requested timestamp to become **fully** available
     *     (all slices committed).
     *
     * Each reader may only appear in a group once.  Re-adding updates the
     * slice-wait threshold but does not create a duplicate entry.
     *
     * @param[in] group   The synchronization group handle.
     * @param[in] reader  The flow reader to add.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowSynchronizationGroupAddReader(mxlFlowSynchronizationGroup group, mxlFlowReader reader);

    /**
     * Add a discrete reader with a **partial-grain** threshold.
     *
     * Like mxlFlowSynchronizationGroupAddReader() but for discrete flows
     * only.  The group's WaitForDataAt() call will return as soon as at
     * least \p minValidSlices of the grain are committed, rather than
     * waiting for the full grain.
     *
     * Re-adding the same reader updates the slice threshold.
     *
     * @param[in] group           The synchronization group handle.
     * @param[in] reader          A discrete flow reader.
     * @param[in] minValidSlices  Minimum slices to wait for per grain.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowSynchronizationGroupAddPartialGrainReader(mxlFlowSynchronizationGroup group, mxlFlowReader reader, uint16_t minValidSlices);

    /**
     * Remove a flow reader from the synchronization group.
     *
     * After removal, the group's wait operation no longer considers this flow.
     *
     * @param[in] group   The synchronization group handle.
     * @param[in] reader  The flow reader to remove.
     * @return MXL_STATUS_OK on success.
     */
    MXL_EXPORT
    mxlStatus mxlFlowSynchronizationGroupRemoveReader(mxlFlowSynchronizationGroup group, mxlFlowReader reader);

    /**
     * Block until data at the specified timestamp is available on **all**
     * flows in the group.
     *
     * The timestamp is converted to a grain/sample index for each flow
     * based on that flow's edit rate.  The function returns as soon as
     * every flow's data at the requested timestamp meets its availability
     * criteria (full grain, partial grain, or sample presence).
     *
     * @param[in] group      The synchronization group handle.
     * @param[in] timestamp  TAI timestamp (nanoseconds since SMPTE ST 2059 epoch).
     * @param[in] timeoutNs  Maximum wait time in nanoseconds.
     * @return MXL_STATUS_OK when all flows have data at the timestamp,
     *         MXL_ERR_TIMEOUT if the timeout expires first.
     */
    MXL_EXPORT
    mxlStatus mxlFlowSynchronizationGroupWaitForDataAt(mxlFlowSynchronizationGroup group, uint64_t timestamp, uint64_t timeoutNs);
#ifdef __cplusplus
}
#endif
