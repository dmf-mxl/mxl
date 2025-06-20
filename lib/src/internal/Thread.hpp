#pragma once

#include "Timing.hpp"

namespace mxl::lib
{
    namespace this_thread
    {
        /**
         * Yield the current thread time slice to other runnable threads.
         */
        void yield() noexcept;

        /**
         * Perform a CPU level yield operation to briefly relieve pressure on shared
         * resources, such as the memory bus during busy loops.
         */
        void yieldProcessor() noexcept;

        /**
         * Sleep for the specified amount of time to pass on the specified clock.
         *
         * \param[in] duration the amount of time to sleep for,
         * \param[in] clock the clock the specified duration refers to.
         * \return The remaining time of the originally specified duration that
         *      was not spent sleeping because the sleep was interrupted for any
         *      reason.
         *
         *      If the remaining time is non-zero the condition that caused the
         *      sleep to abort can be obtained by inspecting errno.
         */
        Duration sleep(Duration duration, Clock clock = Clock::Realtime) noexcept;

        /**
         * Sleep until the specified point in time is reached by the specified
         * clock.
         *
         * \param[in] timepoint the point in time, until which to sleep.
         * \param[in] clock the clock the specified point in time refers to.
         * \return 0 on success, or an error number if the sleep failed to
         *      complete for any reason.
         */
        int sleepUntil(Timepoint timepoint, Clock clock = Clock::Realtime) noexcept;
    }
}
