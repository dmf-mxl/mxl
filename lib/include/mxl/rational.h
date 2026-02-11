// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file rational.h
 * @brief Rational number type and related constants used throughout the MXL SDK.
 *
 * MXL represents frame rates, sample rates, and other time-related quantities
 * as exact rational numbers (numerator / denominator) to avoid floating-point
 * rounding errors.  For example:
 *   - 50 fps video   -> { numerator = 50,    denominator = 1    }
 *   - 29.97 fps NTSC -> { numerator = 30000, denominator = 1001 }
 *   - 48 kHz audio   -> { numerator = 48000, denominator = 1    }
 *
 * This header also defines MXL_UNDEFINED_INDEX, a sentinel value that
 * functions return to signal "no valid index".
 */

#pragma once

#ifdef __cplusplus
#   include <cstdint>
#else
#   include <stdint.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Sentinel value representing an undefined or invalid ring-buffer index.
 *
 * Functions such as mxlGetCurrentIndex() and mxlTimestampToIndex() return
 * this value when the supplied edit-rate is NULL or invalid (e.g., a
 * zero denominator).  Consumer code should always compare against this
 * constant before using a returned index.
 */
#define MXL_UNDEFINED_INDEX UINT64_MAX

    /**
     * An exact rational number expressed as numerator / denominator.
     *
     * Used for frame rates (grainRate), sample rates, and any other
     * quantity where floating-point rounding would be unacceptable.
     * Both fields are signed 64-bit integers so the type can represent
     * negative rates or offsets if future use cases require it.
     */
    typedef struct mxlRational_t
    {
        int64_t numerator;   /**< The top part of the fraction   (e.g., 50 for 50 fps). */
        int64_t denominator; /**< The bottom part of the fraction (e.g., 1  for 50 fps). Must be > 0. */
    } mxlRational;

#ifdef __cplusplus
}
#endif
