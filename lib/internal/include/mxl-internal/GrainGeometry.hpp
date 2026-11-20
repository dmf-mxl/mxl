// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <mxl/flow.h>

namespace mxl::lib
{
    /**
     * How the payload of a grain is laid out: its planes, their slices and the size of a grain. libmxl computes it for
     * the media types it defines, and asks the backend that holds the payload for the media types a backend defines.
     */
    struct GrainGeometry
    {
        /** Size in bytes of the payload of one grain: the sum of the slice lengths times the slice count. */
        std::size_t payloadSize;
        /** Number of slices in each plane of a grain. For video, the number of lines. */
        std::size_t totalSlices;
        /** Length in bytes of one slice of each plane. Non-zero for the planes a grain has, zero after. */
        std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> sliceLengths;
    };
}
