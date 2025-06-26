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
uint64_t mxlGetCurrentHeadIndex(Rational const* editRate)
{
    if ((editRate == nullptr) || (editRate->denominator == 0) || (editRate->numerator == 0))
    {
        return MXL_UNDEFINED_INDEX;
    }

    auto const now = currentTime(mxl::lib::Clock::TAI);
    return now ? mxlTimestampToHeadIndex(editRate, now.value) : MXL_UNDEFINED_INDEX;
}

extern "C"
MXL_EXPORT
uint64_t mxlTimestampToHeadIndex(Rational const* editRate, uint64_t timestamp)
{
    if ((editRate == nullptr) || (editRate->denominator == 0) || (editRate->numerator == 0))
    {
        return MXL_UNDEFINED_INDEX;
    }

    return (timestamp * editRate->numerator + 500'000'000ULL * editRate->denominator) / (1'000'000'000ULL * editRate->denominator);
}

extern "C"
MXL_EXPORT
uint64_t mxlHeadIndexToTimestamp(Rational const* editRate, uint64_t index)
{
    // Validate the edit rate
    if ((editRate == nullptr) || (editRate->denominator == 0) || (editRate->numerator == 0))
    {
        return MXL_UNDEFINED_INDEX;
    }

    return (index * editRate->denominator * 1'000'000'000ULL) / editRate->numerator;
}

extern "C"
MXL_EXPORT
uint64_t mxlGetNsUntilHeadIndex(uint64_t index, Rational const* editRate)
{
    // Validate the edit rate
    if ((editRate == nullptr) || (editRate->denominator == 0) || (editRate->numerator == 0))
    {
        return MXL_UNDEFINED_INDEX;
    }

    // Compute the total nanosecond value since the epoch of the target grain
    auto const targetNs = (index * editRate->denominator * 1'000'000'000ULL) / editRate->numerator;

    auto const nowNs = static_cast<std::uint64_t>(currentTime(mxl::lib::Clock::TAI).value);
    return ((nowNs != 0ULL) && (targetNs >= nowNs)) ? targetNs - nowNs : 0ULL;
}

extern "C"
MXL_EXPORT
void mxlSleepForNs(uint64_t ns)
{
    mxl::lib::this_thread::sleep(mxl::lib::Duration(ns), mxl::lib::Clock::TAI);
}
