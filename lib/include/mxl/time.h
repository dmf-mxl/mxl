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

#define MXL_UNDEFINED_INDEX UINT64_MAX

    /**
     * Get the current ringbuffer index based on the current system TAI time
     * Index 0 is defined to be index at the beginning of the epoch as defined by SMPTE ST 2059
     *
     * \param[in] editRate The edit rate of the Flow
     * \return The index or MXL_UNDEFINED_INDEX if editRate is null or invalid
     */
    MXL_EXPORT
    uint64_t mxlGetCurrentIndex(Rational const* editRate);

    /**
     * Utility method to help compute how many nanoseconds we need to wait until the beginning of the specified index
     * \param[in] index The index to wait for
     * \param[in] editRate The edit rate of the Flow
     * \return How many nanoseconds to wait or MXL_UNDEFINED_INDEX if editRate is null or invalid
     */
    MXL_EXPORT
    uint64_t mxlGetNsUntilIndex(uint64_t index, Rational const* editRate);

    /**
     * Get the current index based on the user provided timespec
     * Index 0 is defined to be index at the beginning of the epoch as defined by SMPTE ST 2059
     *
     * \param[in] editRate The edit rate of the Flow
     * \param[in] timestamp The time stamp in nanoseconds since the epoch.
     * \return The index or MXL_UNDEFINED_INDEX if editRate is null or invalid
     */
    MXL_EXPORT
    uint64_t mxlTimestampToIndex(Rational const* editRate, uint64_t timestamp);

    /**
     * Get the current timestamp based on the user provided index.
     * Index 0 is defined to be index at the beginning of the epoch as defined by SMPTE ST 2059
     *
     * \param[in] editRate The edit rate of the Flow
     * \param[in] index The index in the ringbuffer
     * \return The time stamp in nanoseconds since the epoch or MXL_UNDEFINED_INDEX if editRate is null or invalid
     */
    MXL_EXPORT
    uint64_t mxlIndexToTimestamp(Rational const* editRate, uint64_t index);

    /**
     * Sleep for a specific amount of time.
     * \param[in] ns How long to sleep for, in nanoseconds.
     */
    MXL_EXPORT
    void mxlSleepForNs(uint64_t ns);

    /**
     * Get the current time using the most appropriate clock for the platform.
     *
     * \return The current time in nanoseconds since the epoch.
     */
    MXL_EXPORT
    uint64_t mxlGetTime();

#ifdef __cplusplus
}
#endif