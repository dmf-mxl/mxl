// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file MediaUtils.cpp
 * @brief Media format utility functions for video line stride calculations
 *
 * This file implements calculations for determining the byte-aligned line lengths
 * of various video formats used in broadcast media. These calculations account for
 * the packing schemes used by different formats to efficiently store pixel data.
 *
 * Supported formats:
 * - v210: 10-bit YCbCr 4:2:2 video packed into 32-bit words
 * - 10-bit alpha: 10-bit alpha channel packed into 32-bit words
 *
 * These utilities are critical for correctly allocating grain payload buffers and
 * validating slice lengths in the MXL flow system.
 */

#include "mxl-internal/MediaUtils.hpp"
#include <mxl/platform.h>

/**
 * @brief Calculate the byte length of one line of v210 video
 *
 * v210 is a 10-bit YCbCr 4:2:2 format where pixels are packed into 32-bit words.
 * The packing scheme groups 48 pixels into 128 bytes:
 * - Each 4:2:2 sample (2 pixels) requires 4 components: Cb Y0 Cr Y1
 * - Each component is 10 bits
 * - 6 pixels = 16 components = 160 bits = 5 x 32-bit words = 20 bytes
 * - 48 pixels = 240 components = 2400 bits = 128 bytes
 *
 * The formula rounds up to the nearest 48-pixel boundary to ensure proper alignment.
 *
 * @param width The width of the video line in pixels
 * @return The byte length of one line of v210 video, rounded to 128-byte boundaries
 *
 * @note This is a published SMPTE standard packing format
 * @note The result is always a multiple of 128 bytes
 *
 * Example:
 * - 1920 pixels: (1920 + 47) / 48 * 128 = 40 * 128 = 5120 bytes
 * - 1280 pixels: (1280 + 47) / 48 * 128 = 27 * 128 = 3456 bytes
 */
MXL_EXPORT
std::uint32_t mxl::lib::getV210LineLength(std::size_t width)
{
    // Round up to nearest 48-pixel boundary and convert to bytes
    return static_cast<std::uint32_t>((width + 47) / 48 * 128);
}

/**
 * @brief Calculate the byte length of one line of 10-bit alpha channel data
 *
 * 10-bit alpha data is packed into 32-bit words with 3 pixels per word:
 * - Each alpha sample is 10 bits
 * - 3 samples = 30 bits, stored in a 32-bit word (2 bits unused/padding)
 * - 3 pixels = 4 bytes
 *
 * This format is used for the key/alpha plane in v210a (v210 with alpha) streams.
 * The formula rounds up to the nearest 3-pixel boundary.
 *
 * @param width The width of the alpha line in pixels
 * @return The byte length of one line of 10-bit alpha data, rounded to 4-byte boundaries
 *
 * @note This packing matches the v210a specification for the alpha plane
 * @note The result is always a multiple of 4 bytes
 *
 * Example:
 * - 1920 pixels: (1920 + 2) / 3 * 4 = 640 * 4 = 2560 bytes
 * - 1280 pixels: (1280 + 2) / 3 * 4 = 427 * 4 = 1708 bytes
 */
MXL_EXPORT
std::uint32_t mxl::lib::get10BitAlphaLineLength(std::size_t width)
{
    // Round up to nearest 3-pixel boundary and convert to bytes
    return static_cast<std::uint32_t>((width + 2) / 3 * 4);
}
