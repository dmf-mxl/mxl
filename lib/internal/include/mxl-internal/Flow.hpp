// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Internal shared-memory headers for discrete grains and event slots.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <mxl/flow.h>
#include <mxl/platform.h>
#include "FlowState.hpp"

namespace mxl::lib
{
    /// The version of the flow data structs in shared memory that we expect and support.
    inline constexpr auto FLOW_DATA_VERSION = 1U;

    /// The version of the grain header structs in shared memory that we expect an support.
    inline constexpr auto GRAIN_HEADER_VERSION = 1U;

    ///
    /// Internal Flow structure stored in shared memory
    /// The 'info' field is public and will be returned through the mxl C api
    ///
    struct Flow
    {
        mxlFlowInfo info;
        mxl::lib::FlowState state;
    };

    /// The first 8KiB of a grain are reserved for the mxlGrainInfo structure, including user data.  Ample padding is provided
    /// between the header and the payload.  Payload is page aligned AND AVX512 (64 bytes) aligned.
    inline constexpr auto const MXL_GRAIN_PAYLOAD_OFFSET = std::size_t{8192};

    struct GrainHeader
    {
        mxlGrainInfo info;

        std::uint8_t pad[MXL_GRAIN_PAYLOAD_OFFSET - sizeof info];
    };

    ///
    /// Internal Grain structure stored in shared memory
    /// The 'info' field of the header is public and will be returned through the mxl C api
    ///
    /// The total size of a grain is:
    ///  - payload in host memory : sizeof header + header.info.grainSize
    ///  - payload in device memory : sizeof header
    ///
    struct Grain
    {
        GrainHeader header;
    };

    /// Version stored in the public mxlEventInfo metadata of every published entry.
    inline constexpr auto EVENT_HEADER_VERSION = std::uint32_t{1};

    /** @brief 640-byte slot header with layout fields, sequence and metadata on separate 64-byte boundaries. */
    struct EventHeader
    {
        std::uint32_t version;              ///< Immutable EventRingBuffer storage version.
        std::uint32_t size;                 ///< Immutable size of EventHeader in bytes.
        alignas(64) std::uint64_t sequence; ///< Atomic tag: index shifted left one bit, low bit set after publication; EMPTY if unused.
        alignas(64) std::uint64_t infoWords[sizeof(mxlEventInfo) / sizeof(std::uint64_t)]; ///< Public metadata accessed through atomic words.
    };

    /** @brief Fixed part of an event slot; its payload follows immediately in the mapping. */
    struct Event
    {
        EventHeader header; ///< Sequence, layout and metadata fields for this slot.
    };

    std::ostream& operator<<(std::ostream& os, Grain const& obj);

} // namespace mxl::lib
