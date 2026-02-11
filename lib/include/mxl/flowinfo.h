// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file flowinfo.h
 * @brief Binary layout of the shared-memory flow header (mxlFlowInfo).
 *
 * Every flow in an MXL domain stores a fixed-size (2048-byte) header in
 * the file `${mxlDomain}/${flowId}.mxl-flow/data`.  This header is memory-
 * mapped by both writers and readers and describes everything they need to
 * know about the flow: what type of media it carries, how big the ring
 * buffer is, what the current head index is, etc.
 *
 * The header is split into two logical sections:
 *
 *   - **Config info** (immutable after creation) -- media format, rate,
 *     ring-buffer geometry, payload location.
 *   - **Runtime info** (updated by the writer, read by readers) -- head
 *     index, last write/read timestamps.
 *
 * Memory layout (2048 bytes total):
 * @code
 *   Offset   Size   Field
 *   0x0000   4      version (currently 1)
 *   0x0004   4      size    (always 2048)
 *   0x0008   128    mxlCommonFlowConfigInfo   (UUID, format, rate, hints)
 *   0x0088   64     mxlDiscreteFlowConfigInfo | mxlContinuousFlowConfigInfo
 *   0x00C8   64     mxlFlowRuntimeInfo        (headIndex, timestamps)
 *   0x0108   1784   reserved padding (cache-line alignment)
 * @endcode
 *
 * All structures contain `reserved` padding bytes so that future fields
 * can be added without changing the overall binary size.
 */

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
 * Maximum number of payload planes that a single grain can contain.
 *
 * A "plane" is a contiguous block of pixel/sample data within a grain.
 * Video formats may use multiple planes; for example:
 *   - video/v210  -- 1 plane  (luma + chroma interleaved per V210 spec)
 *   - video/v210a -- 2 planes (fill plane followed by alpha/key plane)
 *
 * Four planes should be sufficient for any foreseeable media format.
 */
#define MXL_MAX_PLANES_PER_GRAIN 4

#ifdef __cplusplus
extern "C"
{
#endif

    /* ======================================================================
     * Payload location
     * ==================================================================== */

    /**
     * Describes where the payload memory of a flow physically resides.
     *
     * HOST_MEMORY is the default: payload bytes live in the tmpfs-backed
     * shared-memory files.  DEVICE_MEMORY indicates that payload lives on
     * a GPU or other accelerator device; in that case, the `deviceIndex`
     * field of mxlCommonFlowConfigInfo identifies which device.
     */
    typedef enum mxlPayloadLocation
    {
        MXL_PAYLOAD_LOCATION_HOST_MEMORY   = 0, /**< Payload is in normal host RAM (mmap-accessible). */
        MXL_PAYLOAD_LOCATION_DEVICE_MEMORY = 1, /**< Payload is on GPU / accelerator memory.          */
    } mxlPayloadLocation;

    /* ======================================================================
     * Common (format-independent) configuration
     * ======================================================================
     * This 128-byte block appears at the start of every flow's config and
     * carries the metadata that is the same regardless of whether the flow
     * is video, audio, or data.
     * ==================================================================== */

    /**
     * Immutable metadata common to ALL flow types (video, audio, data).
     *
     * Stored at the beginning of the flow's `data` shared-memory file.
     * None of these fields change after the flow is created.
     *
     * Total size: 128 bytes (including reserved padding).
     */
    typedef struct mxlCommonFlowConfigInfo_t
    {
        /**
         * The 128-bit (16-byte) UUID of this flow, stored as raw bytes.
         * Matches the {flowId} component in the filesystem path
         * `${mxlDomain}/${flowId}.mxl-flow/`.
         */
        uint8_t id[16];

        /**
         * The media data format carried by this flow.
         * Cast to mxlDataFormat to interpret.
         * @see mxlDataFormat
         */
        uint32_t format;

        /**
         * Bitfield of flow-level flags.
         * Currently no flags are defined; this field is reserved and set to 0.
         */
        uint32_t flags;

        /**
         * The rate at which new grains (or samples) are produced, expressed
         * as a rational number.
         *
         * For VIDEO and DATA (discrete) flows:
         *   This is the frame/grain rate and must match the `grain_rate`
         *   field in the NMOS IS-04 flow descriptor.  E.g. {50, 1} = 50 fps.
         *
         * For AUDIO (continuous) flows:
         *   This is the sample rate.  E.g. {48000, 1} = 48 kHz.
         */
        mxlRational grainRate;

        /**
         * Hint: maximum number of items written in a single commit batch.
         *
         * For continuous flows this is in **samples**; for discrete flows
         * it is in **slices** (typically lines of video).  The writer
         * promises not to exceed this in one mxlFlowWriterCommit*() call.
         *
         * For continuous flows this must be < bufferLength / 2.
         * For discrete flows this must be >= 1.
         */
        uint32_t maxCommitBatchSizeHint;

        /**
         * Hint: maximum batch size at which data-availability is **signalled**
         * to waiting readers (via futex wake).
         *
         * Must be a multiple of maxCommitBatchSizeHint and >= 1.
         * A larger sync batch means fewer futex wakes but higher latency
         * until the reader sees new data.
         */
        uint32_t maxSyncBatchSizeHint;

        /**
         * Where the flow's payload bytes physically live.
         * @see mxlPayloadLocation
         */
        uint32_t payloadLocation;

        /**
         * Device ordinal when payloadLocation == DEVICE_MEMORY.
         * Set to -1 for HOST_MEMORY flows.
         */
        int32_t deviceIndex;

        /**
         * Reserved padding -- keeps the total struct size at 128 bytes.
         * Must be zero-filled.  Do not read or write.
         */
        uint8_t reserved[72];
    } mxlCommonFlowConfigInfo;

    /* ======================================================================
     * Format-specific configuration (discrete vs. continuous)
     * ==================================================================== */

    /**
     * Immutable configuration for a **discrete** (VIDEO or DATA) flow.
     *
     * Discrete flows store individually addressable grains in a fixed-size
     * ring buffer.  Each grain may consist of multiple "slices" -- for
     * video a slice is one scan line; for ancillary data a slice is a byte.
     *
     * Total size: 64 bytes (including reserved padding).
     */
    typedef struct mxlDiscreteFlowConfigInfo_t
    {
        /**
         * Size in bytes of a single slice within each payload plane.
         *
         * The array is indexed by plane number (0..MXL_MAX_PLANES_PER_GRAIN-1).
         * For video/v210 only sliceSizes[0] is meaningful; for video/v210a
         * sliceSizes[0] is the fill-plane line size and sliceSizes[1] is
         * the key-plane line size.  Unused entries are zero.
         *
         * For data flows, a "slice" is a single byte, so sliceSizes[0] == 1.
         */
        uint32_t sliceSizes[MXL_MAX_PLANES_PER_GRAIN];

        /**
         * Number of grain slots in the ring buffer.
         *
         * Matches the number of files in `${mxlDomain}/${flowId}.mxl-flow/grains/`.
         * The actual ring-buffer index for a given grain index is:
         *     ringIndex = grainIndex % grainCount
         */
        uint32_t grainCount;

        /**
         * Reserved padding -- keeps the total struct size at 64 bytes.
         * Must be zero-filled.
         */
        uint8_t reserved[44];
    } mxlDiscreteFlowConfigInfo;

    /**
     * Immutable configuration for a **continuous** (AUDIO) flow.
     *
     * Continuous flows store per-channel ring buffers of raw samples in a
     * single shared-memory file (`channels`).  The memory layout is:
     * @code
     *   channel 0: bufferLength * sampleWordSize bytes
     *   channel 1: bufferLength * sampleWordSize bytes
     *   ...
     *   channel N-1: bufferLength * sampleWordSize bytes
     * @endcode
     *
     * Readers may access at most bufferLength / 2 samples at a time; the
     * other half is reserved as the "write zone" to prevent races.
     *
     * Total size: 64 bytes (including reserved padding).
     */
    typedef struct mxlContinuousFlowConfigInfo_t
    {
        /**
         * Number of independent audio channels.
         * Each channel gets its own ring buffer within the `channels` file.
         */
        uint32_t channelCount;

        /**
         * Number of sample slots in **each** channel ring buffer.
         *
         * Readers can request windows of up to bufferLength / 2 samples.
         * Writers must not commit more than bufferLength / 2 samples
         * before readers have a chance to consume them.
         */
        uint32_t bufferLength;

        /**
         * Reserved padding -- keeps the total struct size at 64 bytes.
         * Must be zero-filled.
         */
        uint8_t reserved[56];
    } mxlContinuousFlowConfigInfo;

    /* ======================================================================
     * Combined flow configuration
     * ==================================================================== */

    /**
     * Complete immutable configuration of a flow.
     *
     * Contains the format-independent `common` block followed by a union
     * of the format-specific blocks.  To interpret the union, inspect
     * `common.format`:
     *   - VIDEO or DATA -> use `discrete`
     *   - AUDIO         -> use `continuous`
     */
    typedef struct mxlFlowConfigInfo_t
    {
        /** Format-independent metadata (UUID, rate, hints, etc.). */
        mxlCommonFlowConfigInfo common;

        /** Format-specific metadata (only one branch is valid per flow). */
        union
        {
            mxlDiscreteFlowConfigInfo   discrete;   /**< Valid when common.format is VIDEO or DATA. */
            mxlContinuousFlowConfigInfo continuous;  /**< Valid when common.format is AUDIO.         */
        };

    } mxlFlowConfigInfo;

    /* ======================================================================
     * Runtime (mutable) flow information
     * ==================================================================== */

    /**
     * Mutable runtime state of a flow, updated by the writer as data
     * is produced and (optionally) by readers when they touch the `access` file.
     *
     * Stored in the same `data` shared-memory file as the config info,
     * immediately following the config block.
     *
     * Total size: 64 bytes (including reserved padding).
     */
    typedef struct mxlFlowRuntimeInfo_t
    {
        /**
         * The grain index (discrete) or sample index (continuous) of the
         * most recently committed item.  This is the "write cursor" of
         * the ring buffer.
         */
        uint64_t headIndex;

        /**
         * TAI timestamp (nanoseconds since the SMPTE ST 2059 epoch) of
         * the last successful write to this flow.
         */
        uint64_t lastWriteTime;

        /**
         * TAI timestamp (nanoseconds since the SMPTE ST 2059 epoch) of
         * the last time a reader accessed this flow.
         */
        uint64_t lastReadTime;

        /**
         * Reserved padding -- keeps the total struct size at 64 bytes.
         * Must be zero-filled.
         */
        uint8_t reserved[40];
    } mxlFlowRuntimeInfo;

    /* ======================================================================
     * Full flow header (the `data` shared-memory file)
     * ==================================================================== */

    /**
     * The top-level binary structure stored in each flow's `data` file.
     *
     * This 2048-byte structure is memory-mapped by both writers (read-write)
     * and readers (read-only).  Its fixed size ensures that:
     *   - It fits within a single page on most architectures.
     *   - Future fields can be added into reserved space without changing
     *     the file size or requiring a migration step.
     *
     * Filesystem location:
     *     `${mxlDomain}/${flowId}.mxl-flow/data`
     */
    typedef struct mxlFlowInfo_t
    {
        /**
         * Structure version number.
         * The only currently supported value is 1.  Readers must reject
         * any value they do not understand.
         */
        uint32_t version;

        /**
         * Total size of this structure in bytes (always 2048).
         * Acts as a consistency check for memory-mapped readers.
         */
        uint32_t size;

        /** Immutable flow configuration (common + format-specific). */
        mxlFlowConfigInfo config;

        /** Mutable runtime state (head index, timestamps). */
        mxlFlowRuntimeInfo runtime;

        /**
         * Reserved padding to bring the total structure size to exactly
         * 2048 bytes.  Ensures cache-line alignment and leaves room for
         * future fields.  Do not read or write.
         */
        uint8_t reserved[1784];
    } mxlFlowInfo;

#ifdef __cplusplus
}
#endif
