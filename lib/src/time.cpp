#include <cstdint>
#include <ctime>
#include <mxl/mxl.h>
#include <mxl/time.h>

#ifdef __APPLE__
#  define MXL_CLOCK CLOCK_REALTIME
#elif __linux__
#  define MXL_CLOCK CLOCK_TAI
#else
#  pragma GCC error "Unsupported platform"
#endif

#define TAI_LEAP_SECONDS 37 // For platforms that do not have a CLOCK_TAI.

extern "C"
MXL_EXPORT
void
mxlGetTime( timespec *out_ts )
{
    ::clock_gettime( MXL_CLOCK, out_ts );
    if ( MXL_CLOCK == CLOCK_REALTIME )
    {
        out_ts->tv_sec += TAI_LEAP_SECONDS;
    }
}

extern "C"
MXL_EXPORT
uint64_t
mxlGetCurrentGrainIndex( const Rational *in_editRate )
{
    if ( in_editRate == nullptr || in_editRate->denominator == 0 || in_editRate->numerator == 0 )
    {
        return MXL_UNDEFINED_OFFSET;
    }

    timespec ts;
    auto res = ::clock_gettime( MXL_CLOCK, &ts );
    if ( 0 != res )
    {
        return MXL_UNDEFINED_OFFSET;
    }
    else
    {
        if ( MXL_CLOCK == CLOCK_REALTIME )
        {
            ts.tv_sec += TAI_LEAP_SECONDS;
        }

        return mxlTimeSpecToGrainIndex( in_editRate, &ts );
    }
}

extern "C"
MXL_EXPORT
uint64_t
mxlTimeSpecToGrainIndex( const Rational *in_editRate, const timespec *in_timespec )
{
    if ( in_editRate == nullptr || in_editRate->denominator == 0 || in_editRate->numerator == 0 || in_timespec == nullptr )
    {
        return MXL_UNDEFINED_OFFSET;
    }

    uint64_t totalNs = in_timespec->tv_sec * 1000000000ULL + in_timespec->tv_nsec;
    uint64_t grainDurationNs = in_editRate->denominator * 1000000000ULL / in_editRate->numerator;
    return totalNs / grainDurationNs;
}

extern "C"
MXL_EXPORT
uint64_t
mxlGetNsUntilGrainIndex( uint64_t in_index, const Rational *in_editRate )
{
    // Validate the edit rate
    if ( in_editRate == nullptr || in_editRate->denominator == 0 || in_editRate->numerator == 0 )
    {
        return MXL_UNDEFINED_OFFSET;
    }

    // Read the current TAI time
    timespec now;
    auto res = clock_gettime( MXL_CLOCK, &now );
    if ( 0 != res )
    {
        return MXL_UNDEFINED_OFFSET;
    }

    if ( MXL_CLOCK == CLOCK_REALTIME )
    {
        now.tv_sec += TAI_LEAP_SECONDS;
    }

    // Convert the current TAI time to nanosec.
    uint64_t nowTotalNs = now.tv_sec * 1000000000ULL + now.tv_nsec;

    // Compute how many nanoseconds a grain lasts
    uint64_t grainDurationNs = in_editRate->denominator * 1000000000ULL / in_editRate->numerator;

    // Compute the total nanosecond value since the epoch of the target grain
    auto targetGrainNs = in_index * grainDurationNs;

    if ( targetGrainNs > nowTotalNs )
    {
        return targetGrainNs - nowTotalNs;
    }
    else
    {
        // we are past the target grain.
        return 0;
    }
}

extern "C"
MXL_EXPORT
void
mxlSleepForNs( uint64_t in_ns )
{
    timespec ts;
    ts.tv_sec = in_ns / 1000000000;
    ts.tv_nsec = in_ns % 1000000000;

#ifdef __linux__
    ::clock_nanosleep( MXL_CLOCK, 0, &ts, nullptr );
#elif __APPLE__
    ::nanosleep( &ts, nullptr );
#else
#pragma GCC error Unsupported platform
#endif
}
