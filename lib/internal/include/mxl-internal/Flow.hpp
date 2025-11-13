// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

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
    /// The version of the flow data structs in shared memory that we expect and support.
    constexpr auto FLOW_DATA_VERSION = 1U;

    /// The version of the grain header structs in shared memory that we expect an support.
    constexpr auto GRAIN_HEADER_VERSION = 1U;

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
    constexpr auto const MXL_GRAIN_PAYLOAD_OFFSET = std::size_t{8192};

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

    std::ostream& operator<<(std::ostream& os, Grain const& obj);

} // namespace mxl::lib
