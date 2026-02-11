// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file time.h
 * @brief Timing utilities -- index/timestamp conversion and sleep helpers.
 *
 * MXL's timing model is built on two pillars:
 *
 *   1. **TAI time** (International Atomic Time) as defined by SMPTE ST 2059.
 *      All timestamps in MXL are expressed as nanoseconds since the
 *      SMPTE ST 2059 epoch (the TAI epoch).
 *
 *   2. **Grain/sample indices** which are simply sequential integers starting
 *      at 0 (epoch).  The relationship between an index and a timestamp is:
 *
 *          grainDurationNs = denominator * 1,000,000,000 / numerator
 *          grainIndex      = timestamp   / grainDurationNs
 *          timestamp       = grainIndex  * grainDurationNs
 *
 * The functions in this header perform these conversions using the rational
 * edit-rate (frame rate or sample rate) to maintain exact integer arithmetic
 * -- no floating-point rounding is involved.
 *
 * Additionally, platform-abstracted sleep/clock functions are provided so
 * that media functions can pace their read/write loops without depending
 * on platform-specific APIs.
 *
 * @note MXL does NOT require a PTP/SMPTE 2059 time source -- it only
 *       leverages the epoch and clock definitions (TAI time) from ST 2059.
 *       Any stable, traceable time source (NTP, PTP, cloud time-sync)
 *       is sufficient.
 */

#pragma once

#ifdef __cplusplus
#   include <cstdint>
#else
#   include <stdint.h>
#endif

#include <mxl/platform.h>
#include <mxl/rational.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ======================================================================
     * Index <-> Timestamp conversion
     * ======================================================================
     * These functions convert between grain/sample indices (sequential
     * integers from the epoch) and TAI timestamps (nanoseconds since the
     * SMPTE ST 2059 epoch).  The conversion uses exact integer arithmetic
     * based on the flow's rational edit-rate (grainRate / sampleRate).
     *
     * Example for 50 fps video (editRate = {50, 1}):
     *   grainDurationNs = 1 * 1,000,000,000 / 50 = 20,000,000 ns
     *   grain index 7  -> timestamp = 7 * 20,000,000 = 140,000,000 ns
     *   timestamp 100,000,000 ns -> grain index = 100,000,000 / 20,000,000 = 5
     * ==================================================================== */

    /**
     * Get the grain/sample index corresponding to the current system TAI time.
     *
     * Reads the system clock, converts the current time to nanoseconds
     * since the SMPTE ST 2059 epoch, and divides by the per-grain/sample
     * duration derived from \p editRate.
     *
     * @param[in] editRate  The flow's grain rate (video) or sample rate (audio).
     *                      Must not be NULL and denominator must be > 0.
     * @return The current index, or MXL_UNDEFINED_INDEX if \p editRate is
     *         NULL or invalid.
     */
    MXL_EXPORT
    uint64_t mxlGetCurrentIndex(mxlRational const* editRate);

    /**
     * Compute how many nanoseconds remain until the start of a given index.
     *
     * Useful for pacing a write loop: a writer can compute the next index
     * it needs to fill, call this function, and sleep for the returned
     * duration before writing.
     *
     * @param[in] index     The target grain/sample index.
     * @param[in] editRate  The flow's grain rate or sample rate.
     * @return Nanoseconds remaining until the start of \p index, or
     *         MXL_UNDEFINED_INDEX if \p editRate is NULL or invalid.
     *         Returns 0 if the index is already in the past.
     */
    MXL_EXPORT
    uint64_t mxlGetNsUntilIndex(uint64_t index, mxlRational const* editRate);

    /**
     * Convert an absolute TAI timestamp to a grain/sample index.
     *
     * Performs the integer division:
     *     index = timestamp / (denominator * 1,000,000,000 / numerator)
     *
     * @param[in] editRate   The flow's grain rate or sample rate.
     * @param[in] timestamp  Nanoseconds since the SMPTE ST 2059 epoch.
     * @return The corresponding index, or MXL_UNDEFINED_INDEX if
     *         \p editRate is NULL or invalid.
     */
    MXL_EXPORT
    uint64_t mxlTimestampToIndex(mxlRational const* editRate, uint64_t timestamp);

    /**
     * Convert a grain/sample index back to an absolute TAI timestamp.
     *
     * Performs the multiplication:
     *     timestamp = index * (denominator * 1,000,000,000 / numerator)
     *
     * @param[in] editRate  The flow's grain rate or sample rate.
     * @param[in] index     The grain/sample index to convert.
     * @return Nanoseconds since the SMPTE ST 2059 epoch, or
     *         MXL_UNDEFINED_INDEX if \p editRate is NULL or invalid.
     */
    MXL_EXPORT
    uint64_t mxlIndexToTimestamp(mxlRational const* editRate, uint64_t index);

    /* ======================================================================
     * Clock and sleep utilities
     * ======================================================================
     * Platform-abstracted helpers so callers do not need to use
     * clock_gettime / nanosleep / etc. directly.
     * ==================================================================== */

    /**
     * Sleep (block the calling thread) for the specified number of nanoseconds.
     *
     * @param[in] ns  Duration to sleep, in nanoseconds.
     */
    MXL_EXPORT
    void mxlSleepForNs(uint64_t ns);

    /**
     * Sleep (block the calling thread) until the specified absolute TAI time.
     *
     * If \p timestamp is already in the past, returns immediately.
     *
     * @param[in] timestamp  Target wake-up time in nanoseconds since the
     *                       SMPTE ST 2059 epoch.
     */
    MXL_EXPORT
    void mxlSleepUntil(uint64_t timestamp);

    /**
     * Read the current system time using the most appropriate clock.
     *
     * On Linux this typically uses CLOCK_TAI (if available) or falls back
     * to CLOCK_REALTIME.  The returned value is in nanoseconds since the
     * SMPTE ST 2059 / TAI epoch.
     *
     * @return Current TAI time in nanoseconds since the epoch.
     */
    MXL_EXPORT
    uint64_t mxlGetTime(void);

#ifdef __cplusplus
}
#endif
