// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mxl/flow.h>

namespace mxl::lib
{

    ///
    /// Internal Flow structure stored in shared memory
    /// The 'info' field is public and will be returned through the mxl C api
    ///
    struct Flow
    {
        FlowInfo info;
    };

    std::ostream& operator<<(std::ostream& os, Flow const& obj);

    /// The first 8KiB of a grain are reserved for the GrainInfo structure, including user data.  Ample padding is provided
    /// between the header and the payload.  Payload is page aligned AND AVX512 (64 bytes) aligned.
    constexpr auto const MXL_GRAIN_PAYLOAD_OFFSET = std::size_t{8192};

    struct GrainHeader
    {
        GrainInfo info;

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
