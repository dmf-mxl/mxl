// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file MediaUtils.hpp
 * @brief Media format calculations (line lengths, strides, padding)
 *
 * MXL supports various professional video formats, each with specific packing and alignment rules.
 * These utilities calculate memory layout parameters needed for grain size allocation.
 *
 * V210 format:
 * - YUV 4:2:2 10-bit packed format (SMPTE 274M-2008)
 * - Packs 6 pixels (12 samples: 6Y, 3U, 3V) into 16 bytes (four 32-bit words)
 * - Each 32-bit word contains 3 samples (each 10 bits, with 2 bits padding)
 * - Line length must be multiple of 128 bytes (SMPTE requirement)
 * - Used for uncompressed HD/UHD video interchange
 *
 * 10-bit Alpha format:
 * - Separate alpha channel for compositing operations
 * - Packs 3 alpha samples per 32-bit word (10 bits each, 2 bits padding)
 * - Accompanies V210 video for keying/transparency
 *
 * Why these functions matter:
 * - FlowWriter needs to allocate correct grain sizes
 * - FlowReader needs to validate received grain sizes
 * - Incorrect line lengths cause memory corruption or artifact stripes
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace mxl::lib
{
    /**
     * Calculate the memory length (in bytes) of one scan line in V210 format.
     *
     * V210 packing:
     * - 6 pixels = 12 samples = 16 bytes (four 32-bit words)
     * - Each word: [10-bit sample][10-bit sample][10-bit sample][2-bit pad]
     * - Line must be padded to 128-byte boundary per SMPTE 274M
     *
     * @param width Frame width in pixels (horizontal resolution)
     * @return Line length in bytes, including SMPTE-mandated padding
     *
     * Example: 1920 pixels → (1920/6) * 16 = 5120 bytes (already 128-byte aligned)
     * Example: 1280 pixels → (1280/6) * 16 = 3413.33 → rounds to 3456 bytes (128-byte aligned)
     */
    std::uint32_t getV210LineLength(std::size_t width);

    /**
     * Calculate the memory length (in bytes) of one scan line in 10-bit alpha format.
     *
     * 10-bit Alpha packing:
     * - 3 alpha samples per 32-bit word (10 bits each, 2 bits padding)
     * - May require alignment/padding depending on implementation
     *
     * @param width Frame width in pixels (number of alpha samples per line)
     * @return Line length in bytes, including any required padding
     *
     * Typical use: Allocate separate alpha grain to accompany V210 video grain.
     */
    std::uint32_t get10BitAlphaLineLength(std::size_t width);

}
