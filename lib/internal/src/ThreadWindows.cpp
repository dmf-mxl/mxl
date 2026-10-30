// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// The Windows implementation of Thread.hpp. Thread.cpp holds the POSIX one.

#include <cerrno>
#include <windows.h>
#include <mxl/platform.h>
#include "mxl-internal/Thread.hpp"
#include "WindowsTimer.hpp"

namespace mxl::lib
{
    namespace
    {
        /** A high resolution waitable timer, one per thread, created on first use. */
        class ThreadTimer
        {
        public:
            /** Create the timer. On failure the handle is null. */
            ThreadTimer() noexcept
                : _handle{::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)}
            {}

            ThreadTimer(ThreadTimer const&) = delete;
            ThreadTimer& operator=(ThreadTimer const&) = delete;

            /** Close the timer. */
            ~ThreadTimer()
            {
                if (_handle != nullptr)
                {
                    ::CloseHandle(_handle);
                }
            }

            /** \return The timer handle, or null if it could not be created. */
            [[nodiscard]]
            HANDLE get() const noexcept
            {
                return _handle;
            }

        private:
            /** The timer handle. */
            HANDLE _handle;
        };

        /**
         * Sleep with the high resolution timer of the thread. Windows versions before 10 1803 cannot create such a timer, and fall back to
         * Sleep().
         * \return true if the full duration was slept.
         */
        bool sleepFor(Duration duration) noexcept
        {
            if (duration.value <= 0)
            {
                return true;
            }

            auto const timer = static_cast<HANDLE>(threadTimer());
            if (timer == nullptr)
            {
                ::Sleep(static_cast<DWORD>((duration.value + 999'999) / 1'000'000));
                return true;
            }

            // Negative due times are relative, in 100 ns units.
            auto dueTime = LARGE_INTEGER{};
            dueTime.QuadPart = -((duration.value + 99) / 100);
            if (::SetWaitableTimer(timer, &dueTime, 0, nullptr, nullptr, FALSE) == FALSE)
            {
                return false;
            }
            return ::WaitForSingleObject(timer, INFINITE) == WAIT_OBJECT_0;
        }
    }

    void* threadTimer() noexcept
    {
        thread_local auto const timer = ThreadTimer{};
        return timer.get();
    }

    namespace this_thread
    {
        void yield() noexcept
        {
            ::SwitchToThread();
        }

        void yieldProcessor() noexcept
        {
            YieldProcessor();
        }

        MXL_EXPORT
        Duration sleep(Duration duration, [[maybe_unused]] Clock clock) noexcept
        {
            // The duration is relative, so the clock does not matter.
            if (!sleepFor(duration))
            {
                errno = EINTR;
                return duration;
            }
            return Duration{};
        }

        MXL_EXPORT
        int sleepUntil(Timepoint timepoint, Clock clock) noexcept
        {
            auto const now = currentTime(clock);
            if ((timepoint > now) && !sleepFor(timepoint - now))
            {
                return EINTR;
            }
            return 0;
        }
    }
}
