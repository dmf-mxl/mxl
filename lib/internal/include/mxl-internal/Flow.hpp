// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Flow.hpp
 * @brief Shared memory layout structures for Flow and Grain
 *
 * This file defines the actual memory layout of structures in MXL's shared memory files.
 * These must be POD (Plain Old Data) types with no vtables, RTTI, or complex destructors.
 *
 * MEMORY LAYOUT OVERVIEW:
 *
 * Flow file (${domain}/${flowId}.mxl-flow/data):
 *   [Flow structure]
 *     - mxlFlowInfo info  (ring buffer params, format, dimensions, etc.)
 *     - FlowState state   (inode, syncCounter)
 *   [Derived flow-specific data...]
 *     - DiscreteFlowState: adds grainCount, grainSize, etc.
 *     - ContinuousFlowState: adds sampleOffset, sampleCount, etc.
 *
 * Grain file (${domain}/${flowId}.mxl-flow/grains/data.N):
 *   [GrainHeader - 8192 bytes]
 *     - mxlGrainInfo info (timestamps, flags, slice counts)
 *     - Padding to 8192 bytes
 *   [Payload - variable size]
 *     - Video pixels, ancillary packets, etc.
 *
 * WHY 8192-BYTE GRAIN HEADER?
 * - Page alignment (typical page size = 4096, we use 2 pages)
 * - AVX-512 alignment (64 bytes) - enables SIMD operations on payload
 * - Room for future expansion without breaking ABI
 * - Allows embedding user metadata in grain header
 *
 * VERSIONING:
 * - FLOW_DATA_VERSION: Increment if Flow/FlowState layout changes
 * - GRAIN_HEADER_VERSION: Increment if GrainHeader/mxlGrainInfo layout changes
 * - Readers check version on open, reject incompatible versions
 * - Enables graceful evolution of shared memory format
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <mxl/flow.h>
#include <mxl/platform.h>
#include "FlowInfo.hpp"
#include "FlowState.hpp"

namespace mxl::lib
{
    /**
     * Version number for Flow structure layout in shared memory.
     * Writers set this; readers verify compatibility.
     * Increment if sizeof(Flow) or field layout changes.
     */
    constexpr auto FLOW_DATA_VERSION = 1U;

    /**
     * Version number for Grain structure layout in shared memory.
     * Writers set this; readers verify compatibility.
     * Increment if sizeof(Grain) or field layout changes.
     */
    constexpr auto GRAIN_HEADER_VERSION = 1U;

    /**
     * Internal Flow structure stored in the "data" shared memory file.
     *
     * Memory layout:
     *   offset 0: mxlFlowInfo info (public API structure with flow metadata)
     *   offset X: FlowState state  (internal synchronization fields)
     *
     * This is the base structure; derived classes (DiscreteFlowState, ContinuousFlowState)
     * extend this with flow-type-specific fields.
     *
     * The 'info' field is directly returned to C API users (zero-copy).
     *
     * Size: Varies by derived type (DiscreteFlowState ~200 bytes, ContinuousFlowState ~150 bytes)
     */
    struct Flow
    {
        mxlFlowInfo info;            // Public flow metadata (ring buffer params, format, dimensions)
        mxl::lib::FlowState state;   // Internal sync state (inode, futex counter)
    };

    /**
     * Grain header size in bytes.
     *
     * Why 8192 (8 KiB)?
     * - 2x typical page size (4096) for page-aligned payload
     * - AVX-512 alignment (64 bytes) for SIMD-friendly payload start
     * - Room for mxlGrainInfo (currently ~100 bytes) plus future extensions
     * - Room for user metadata embedded in grain header
     *
     * Payload starts at offset 8192 from beginning of grain file.
     */
    constexpr auto const MXL_GRAIN_PAYLOAD_OFFSET = std::size_t{8192};

    /**
     * Grain header structure (fixed 8192-byte header before payload).
     *
     * Memory layout:
     *   offset 0: mxlGrainInfo info (timestamps, slice counts, flags)
     *   offset sizeof(mxlGrainInfo): padding to 8192 bytes
     */
    struct GrainHeader
    {
        mxlGrainInfo info;  // Public grain metadata (timestamp, size, slices, etc.)

        /**
         * Padding to reach 8192 bytes total.
         * This space can be used for:
         * - User metadata (custom per-grain data)
         * - Future MXL extensions (without ABI break)
         * - Alignment/cache line optimization
         */
        std::uint8_t pad[MXL_GRAIN_PAYLOAD_OFFSET - sizeof info];
    };

    /**
     * Internal Grain structure stored in grain shared memory files.
     *
     * Memory layout in ${domain}/${flowId}.mxl-flow/grains/data.N:
     *   offset 0-8191: GrainHeader (metadata + padding)
     *   offset 8192+:  Payload (video pixels, ancillary data, etc.)
     *
     * The total size of a grain file depends on payload location:
     * - Host memory (typical): sizeof(GrainHeader) + header.info.grainSize
     * - Device memory (GPU/DMA): sizeof(GrainHeader) only (payload elsewhere)
     *
     * The 'info' field is directly returned to C API users (zero-copy).
     */
    struct Grain
    {
        GrainHeader header;  // Fixed 8192-byte header
        // Payload follows immediately after header in memory/file
    };

    /**
     * Stream output operator for Grain debugging.
     * Formats grain metadata (timestamp, size, slices) as human-readable text.
     */
    std::ostream& operator<<(std::ostream& os, Grain const& obj);

} // namespace mxl::lib
