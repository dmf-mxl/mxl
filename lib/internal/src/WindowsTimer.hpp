// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace mxl::lib
{
    /**
     * Get the high resolution waitable timer of the calling thread, created on first use. Sleeps and timed waits use it, because Sleep() and
     * the time outs of wait functions are rounded to the system timer resolution, 15.6 ms by default. SetWaitableTimer() arms the timer and
     * resets it, so each user arms it before it waits on it.
     * \return The timer handle, or null if Windows cannot create such a timer (versions before Windows 10 1803).
     */
    void* threadTimer() noexcept;
}
