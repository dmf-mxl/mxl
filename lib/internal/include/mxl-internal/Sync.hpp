// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Sync.hpp
 * @brief Futex-based synchronization primitives for shared memory
 *
 * MXL uses Linux futex (fast userspace mutex) for cross-process synchronization.
 * This is critical for zero-copy operation because:
 * - Traditional POSIX mutexes require PROT_WRITE (conflicts with reader's PROT_READ)
 * - Futexes work on any memory address (can be in PROT_READ mappings)
 * - Futexes are extremely efficient (kernel involvement only on actual contention)
 *
 * How it works:
 * 1. FlowWriter updates a counter (e.g., grainCount) in shared memory
 * 2. FlowWriter calls wakeAll() on that counter's address
 * 3. FlowReaders use waitUntilChanged() to sleep until the counter changes
 * 4. Futex checks if value still matches expected; if not, returns immediately (no kernel call)
 * 5. If value matches, kernel puts thread to sleep until wakeAll() is called
 *
 * This allows readers with PROT_READ mappings to efficiently wait for writer updates
 * without polling or requiring write permissions.
 *
 * Template parameter T is typically uint32_t or uint64_t (futex works with atomic word sizes).
 */

#pragma once

#include <cstdint>
#include "Timing.hpp"

namespace mxl::lib
{
    /**
     * Wait until the value at *in_addr changes from in_expected, or until deadline.
     * Uses Linux futex(FUTEX_WAIT_BITSET) for efficient cross-process wait.
     *
     * Typical usage in FlowReader:
     *   while (!haveNewGrain) {
     *       auto oldCount = state->grainCount;
     *       if (!waitUntilChanged(&state->grainCount, oldCount, deadline)) {
     *           return TIMEOUT; // Deadline expired
     *       }
     *   }
     *
     * @param in_addr Address to monitor (must be 4-byte aligned for futex)
     * @param in_expected Expected current value (wait aborts immediately if different)
     * @param in_deadline Absolute deadline from Clock::Realtime
     * @return true if value changed, false if timeout expired
     *
     * Thread-safety: Multiple threads can wait on the same address concurrently.
     * Memory ordering: Uses atomic loads internally to ensure visibility.
     */
    template<typename T>
    bool waitUntilChanged(T const* in_addr, T in_expected, Timepoint in_deadline);

    /**
     * Wait until the value at *in_addr changes from in_expected, or until timeout.
     * Relative-timeout version of waitUntilChanged (deadline calculated internally).
     *
     * @param in_addr Address to monitor
     * @param in_expected Expected current value
     * @param in_timeout Relative duration to wait
     * @return true if value changed, false if timeout expired
     *
     * Note: For precise timing, prefer the absolute deadline version to avoid drift.
     */
    template<typename T>
    bool waitUntilChanged(T const* in_addr, T in_expected, Duration in_timeout);

    /**
     * Wake a single thread waiting on in_addr.
     * Uses Linux futex(FUTEX_WAKE, 1).
     *
     * Use this when only one reader needs to be notified (rare in MXL).
     * Most FlowWriter operations use wakeAll() to notify all readers.
     *
     * @param in_addr Address to signal
     */
    template<typename T>
    void wakeOne(T const* in_addr);

    /**
     * Wake all threads waiting on in_addr.
     * Uses Linux futex(FUTEX_WAKE, INT_MAX).
     *
     * Typical usage in FlowWriter after publishing a grain:
     *   state->grainCount++;  // Update shared memory
     *   wakeAll(&state->grainCount);  // Notify all waiting readers
     *
     * @param in_addr Address to signal
     *
     * Thread-safety: Safe to call concurrently from multiple writers.
     * Performance: Only enters kernel if there are actual waiters; no-op otherwise.
     */
    template<typename T>
    void wakeAll(T const* in_addr);
}
