#pragma once

#include <cstdint>
#include "Timing.hpp"

namespace mxl::lib
{
    /**
     * Wait until *in_addr changes or timeout expires.
     *
     * \param in_addr The memory address to monitor. Must refer to a 32 bit value.
     * \param in_expected The initial value expected at in_addr
     * \param in_deadline Until when to wait. Timepoint is expected to come from Clock::Realtime.
     * \return true if value changed, false if timeout expired
     */
    template<typename T>
    bool waitUntilChanged(T const* in_addr, T in_expected, Timepoint in_deadline);

    /**
     * Wait until *in_addr changes or timeout expires.
     *
     * \param in_addr The memory address to monitor. Must refer to a 32 bit value.
     * \param in_expected The initial value expected at in_addr
     * \param in_timeout How long to wait.
     * \return true if value changed, false if timeout expired
     */
    template<typename T>
    bool waitUntilChanged(T const* in_addr, T in_expected, Duration in_timeout);

    /**
     * Wake a single waiter waiting on in_addr
     *
     * \param in_addr The memory address to signal. Must refer to a 32 bit value.
     */
    template<typename T>
    void wakeOne(T const* in_addr);

    /**
     * Wake all waiters waiting on in_addr
     *
     * \param in_addr The memory address to signal. Must refer to a 32 bit value.
     */
    template<typename T>
    void wakeAll(T const* in_addr);
}
