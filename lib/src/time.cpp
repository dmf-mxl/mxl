// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl/time.h"
#include "mxl-internal/IndexConversion.hpp"
#include "mxl-internal/Thread.hpp"
#include "mxl-internal/Timing.hpp"

extern "C"
MXL_EXPORT
uint64_t mxlGetTime()
{
    return currentTime(mxl::lib::Clock::TAI).value;
}

extern "C"
MXL_EXPORT
uint64_t mxlGetCurrentIndex(mxlRational const* editRate)
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
uint64_t mxlTimestampToIndex(mxlRational const* editRate, uint64_t timestamp)
{
    return (editRate != nullptr) ? mxl::lib::timestampToIndex(*editRate, mxl::lib::Timepoint(timestamp)) : MXL_UNDEFINED_INDEX;
}

extern "C"
MXL_EXPORT
uint64_t mxlIndexToTimestamp(mxlRational const* editRate, uint64_t index)
{
    if (editRate != nullptr)
    {
        auto const res = mxl::lib::indexToTimestamp(*editRate, index);
        return (res || (index == 0)) ? res.value : MXL_UNDEFINED_INDEX;
    }
    return MXL_UNDEFINED_INDEX;
}

extern "C"
MXL_EXPORT
uint64_t mxlGetNsUntilIndex(uint64_t index, mxlRational const* editRate)
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

extern "C"
MXL_EXPORT
void mxlSleepUntil(uint64_t timestamp)
{
    mxl::lib::this_thread::sleepUntil(mxl::lib::Timepoint(timestamp), mxl::lib::Clock::TAI);
}
