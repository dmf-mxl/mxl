// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Thread.hpp
 * @brief Thread control utilities for sleeping, yielding, and cooperative scheduling
 *
 * MXL is designed for real-time media applications with strict timing requirements.
 * These utilities help manage CPU resources efficiently:
 *
 * - yield(): Give up CPU to other threads (scheduler-level yield)
 * - yieldProcessor(): Pause instruction for spin-wait loops (hardware-level hint)
 * - sleep(): Sleep for a duration (uses clock_nanosleep internally)
 * - sleepUntil(): Sleep until an absolute deadline (critical for A/V sync)
 *
 * Why we need these:
 * - FlowReaders may spin-wait for new grains in low-latency mode
 * - FlowWriters may need to throttle to match media rate
 * - DomainWatcher polling loop needs cooperative scheduling
 * - All sleeping respects the specified Clock (TAI, Monotonic, etc.)
 */

#pragma once

#include "Timing.hpp"

namespace mxl::lib
{
    namespace this_thread
    {
        /**
         * Yield the current thread's time slice to other runnable threads.
         * Maps to sched_yield() on POSIX. Use this when waiting for another thread
         * to complete work, to avoid burning CPU while still maintaining responsiveness.
         */
        void yield() noexcept;

        /**
         * Perform a CPU-level yield operation (PAUSE instruction on x86).
         * This is lighter-weight than yield() and is appropriate for spin-wait loops
         * where you're checking shared memory that might change soon. It:
         * - Reduces power consumption during spin-waits
         * - Improves performance of hyper-threaded CPUs by relieving memory bus pressure
         * - Avoids context switch overhead of yield()
         *
         * Typical use: while (!condition) { yieldProcessor(); }
         */
        void yieldProcessor() noexcept;

        /**
         * Sleep for the specified duration, relative to the specified clock.
         * Maps to clock_nanosleep(TIMER_ABSTIME=0) internally.
         *
         * @param duration How long to sleep
         * @param clock Which clock to measure duration against (default: Realtime)
         * @return Remaining unsleep time if interrupted (check errno for reason).
         *         If non-zero, sleep was interrupted by signal or other event.
         *
         * Note: For frame-accurate timing, prefer sleepUntil() with absolute TAI timestamps.
         */
        Duration sleep(Duration duration, Clock clock = Clock::Realtime) noexcept;

        /**
         * Sleep until the specified absolute timepoint is reached on the specified clock.
         * Maps to clock_nanosleep(TIMER_ABSTIME=1) internally.
         *
         * This is the preferred way to schedule media operations because:
         * - Absolute deadlines avoid drift from repeated relative sleeps
         * - Works with Clock::TAI for frame-accurate A/V sync
         * - Immune to system time changes (when using TAI or Monotonic)
         *
         * @param timepoint Absolute deadline to sleep until
         * @param clock Which clock the timepoint refers to (default: Realtime)
         * @return 0 on success, errno value if sleep failed or was interrupted
         *
         * Example: sleepUntil(nextFrameTimestamp, Clock::TAI)
         */
        int sleepUntil(Timepoint timepoint, Clock clock = Clock::Realtime) noexcept;
    }
}
