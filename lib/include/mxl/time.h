#pragma once

#ifdef __cplusplus
#  include <cstdint>
#  include <ctime>
#else
#  include <stdint.h>
#  include <time.h>
#endif

#include <mxl/mxl.h>

#ifdef __cplusplus
extern "C"
{
#endif


/**
 * Get the current grain index based on the current system TAI time
 * 0 = first grain since epoch as defined by SMPTE ST 2059
 *
 * \param in_editRate The edit rate of the Flow
 * \return The grain index or MAXUINT64 if in_grainRate is null or invalid (0 denominator, 0/1, etc.)
 */
MXL_EXPORT uint64_t mxlGetCurrentGrainIndex( const Rational *in_editRate );

/**
 * Utility method to help compute how many nanoseconds we need to wait until the beginning of the specified grain
 * \param in_index The grain to wait for
 * \param in_editRate The edit rate of the Flow
 * \return How many nanoseconds to wait or MAXUINT64 if in_grainRate is null or invalid (0 denominator, 0/1, etc.)
 */
MXL_EXPORT uint64_t mxlGetNsUntilGrainIndex( uint64_t in_index, const Rational *in_editRate );

/**
 * Get the current grain index based on the user provided timespec
 * 0 = first grain since epoch as defined by SMPTE ST 2059
 *
 * \param in_editRate The edit rate of the Flow
 * \param in_timespec The user provided time
 * \return The grain index or MAXUINT64 if in_grainRate is null or invalid (0 denominator, 0/1, etc.)
 */
MXL_EXPORT uint64_t mxlTimeSpecToGrainIndex( const Rational *in_editRate, const struct timespec *in_timespec );

/**
 * Sleep for a specific amount of time.
 * \param in_ns How long to sleep for, in nanoseconds.
 */
MXL_EXPORT void mxlSleepForNs( uint64_t in_ns );

/**
 * Get the current time using the most appropriate clock for the platform
 *
 * \param out_ts The current time.
 */
MXL_EXPORT void mxlGetTime( struct timespec *out_ts );

#ifdef __cplusplus
}
#endif