// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#include "Thread.hpp"
#include <cerrno>
#include <sched.h>
#include "detail/ClockHelpers.hpp"

namespace mxl::lib
{
    namespace this_thread
    {
        void yield() noexcept
        {
            ::sched_yield();
        }

        void yieldProcessor() noexcept
        {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#elif defined(__ARM_ARCH) && (__ARM_ARCH >= 7)
            asm volatile("dmb ish" ::: "memory");
#else
            // At least emulate the side effect of a compiler
            // read/write barrier.
            asm volatile("" ::: "memory")
#endif
        }

        Duration sleep(Duration duration, [[maybe_unused]] Clock clock) noexcept
        {
            auto const sleepTime = asTimeSpec(duration);
            std::timespec remainder;

            auto const req = &sleepTime;
            auto const rem = &remainder;

#if defined(__linux__)
            auto const result = ::clock_nanosleep(detail::clockToId(clock), 0, req, rem);
            errno = result;
#elif defined(__APPLE__)
            auto const result = ::nanosleep(req, rem);
#else
#   pragma GCC error No this_thread::sleep() implementation available for the current platform.
#endif
            // Please be aware that nanosleep() and clock_nanosleep() have
            // different domains for their return value, but in either case
            // a value of 0 means the call completed successfully.
            return (result == 0) ? Duration{} : asDuration(remainder);
        }

        int sleepUntil(Timepoint timepoint, Clock clock) noexcept
        {
#if defined(__linux__)
            auto const sleepTime = asTimeSpec(timepoint);
            auto const result = ::clock_nanosleep(detail::clockToId(clock), TIMER_ABSTIME, &sleepTime, nullptr);
            return result;
#elif defined(__APPLE__)

            auto const now = currentTime(clock);
            if (timepoint > now)
            {
                auto const sleepTime = asTimeSpec(timepoint - now);
                auto const result = ::nanosleep(&sleepTime, nullptr);
                if (result)
                {
                    return errno;
                }
            }
            return 0;
#else
#   pragma GCC error No this_thread::sleepUntil() implementation available for the current platform.
#endif
        }
    }
}
