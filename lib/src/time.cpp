#include "mxl/time.h"
#include "internal/Thread.hpp"
#include "internal/Timing.hpp"

extern "C"
MXL_EXPORT
uint64_t mxlGetTime()
{
    return currentTime(mxl::lib::Clock::TAI).value;
}

extern "C"
MXL_EXPORT
uint64_t mxlGetCurrentIndex(Rational const* editRate)
{
    if ((editRate == nullptr) || (editRate->denominator == 0) || (editRate->numerator == 0))
    {
        return MXL_UNDEFINED_INDEX;
    }

    auto const now = mxlGetTime();
    return now ? mxlTimestampToIndex(editRate, now) : MXL_UNDEFINED_INDEX;
}

extern "C"
MXL_EXPORT
uint64_t mxlTimestampToIndex(Rational const* editRate, uint64_t timestamp)
{
    if ((editRate == nullptr) || (editRate->denominator == 0) || (editRate->numerator == 0))
    {
        return MXL_UNDEFINED_INDEX;
    }

    return static_cast<uint64_t>((timestamp * __int128_t{editRate->numerator} + 500'000'000 * __int128_t{editRate->denominator}) /
                                 (1'000'000'000 * __int128_t{editRate->denominator}));
}

extern "C"
MXL_EXPORT
uint64_t mxlIndexToTimestamp(Rational const* editRate, uint64_t index)
{
    // Validate the edit rate
    if ((editRate == nullptr) || (editRate->denominator == 0) || (editRate->numerator == 0))
    {
        return MXL_UNDEFINED_INDEX;
    }

    return static_cast<uint64_t>(
        (index * __int128_t{editRate->denominator} * 1'000'000'000 + __int128_t{editRate->numerator} / 2) / __int128_t{editRate->numerator});
}

extern "C"
MXL_EXPORT
uint64_t mxlGetNsUntilIndex(uint64_t index, Rational const* editRate)
{
    // Validate the edit rate
    if ((editRate == nullptr) || (editRate->denominator == 0) || (editRate->numerator == 0))
    {
        return MXL_UNDEFINED_INDEX;
    }

    auto const targetNs = mxlIndexToTimestamp(editRate, index);
    auto const nowNs = mxlGetTime();
    return ((nowNs != 0ULL) && (targetNs >= nowNs)) ? targetNs - nowNs : 0ULL;
}

extern "C"
MXL_EXPORT
void mxlSleepForNs(uint64_t ns)
{
    mxl::lib::this_thread::sleep(mxl::lib::Duration(ns), mxl::lib::Clock::TAI);
}
