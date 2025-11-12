// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/Sync.hpp"
#include <cerrno>
#include <climits>
#include <cstdint>
#include <atomic>
#include <unistd.h>
#if defined(__linux__)
#   include <sys/syscall.h>
#   include <bits/time.h>
#   include <linux/futex.h>
#elif defined(__APPLE__)
#   include <os/os_sync_wait_on_address.h>
#endif
#include "mxl-internal/Logging.hpp"

namespace mxl::lib
{
    namespace
    {
        template<typename T, typename U>
        constexpr inline auto const same_properties_as_v = (sizeof(T) == sizeof(U)) && (alignof(T) == alignof(U));

        template<typename T>
        constexpr inline auto const valid_specialization_v = same_properties_as_v<T, std::int32_t>;

#if defined(__linux__)
        int do_wait(void const* futex, std::uint32_t expected, Duration timeout)
        {
            auto const timeoutTs = asTimeSpec(timeout);
            return ::syscall(SYS_futex, futex, FUTEX_WAIT, expected, &timeoutTs, nullptr, 0);
        }

        int do_wake_one(void const* futex)
        {
            return ::syscall(SYS_futex, futex, FUTEX_WAKE, 1, nullptr, nullptr, 0);
        }

        int do_wake_all(void const* futex)
        {
            return ::syscall(SYS_futex, futex, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
        }

#elif defined(__APPLE__)
        int do_wait(void const* futex, std::uint32_t expected, Duration timeout)
        {
            return ::os_sync_wait_on_address_with_timeout(
                const_cast<void*>(futex), expected, sizeof(std::uint32_t), OS_SYNC_WAIT_ON_ADDRESS_SHARED, OS_CLOCK_MACH_ABSOLUTE_TIME, timeout.value);
        }

        int do_wake_one(void const* futex)
        {
            return ::os_sync_wake_by_address_any(const_cast<void*>(futex), sizeof(std::uint32_t), OS_SYNC_WAKE_BY_ADDRESS_SHARED);
        }

        int do_wake_all(void const* futex)
        {
            return ::os_sync_wake_by_address_all(const_cast<void*>(futex), sizeof(std::uint32_t), OS_SYNC_WAKE_BY_ADDRESS_SHARED);
        }
#else
#   pragma GCC error "Unsupported platform"
#endif
    }

    template<typename T>
    bool waitUntilChanged(T const* in_addr, T in_expected, Timepoint in_deadline)
    {
        static_assert(valid_specialization_v<T>, "Only 32 bit types with natural alignment are supported.");

        auto syncObject = std::atomic_ref{*in_addr};
        while (syncObject.load(std::memory_order_acquire) == in_expected)
        {
            auto const now = currentTime(Clock::Realtime);
            if (now >= in_deadline)
            {
                MXL_DEBUG("Deadline already reached");
                return false;
            }

            // ATTENTION: Do not check for != 0, as positive values are success indicators
            //      in the macOS implementation of this function.
            // NOTE: Unfortunately syncObject.address() is only available from C++26 onwards
            if (auto const ret = do_wait(in_addr, in_expected, in_deadline - now); ret == -1)
            {
                switch (errno)
                {
                    case EAGAIN:
                        // On linux EAGAIN indicates that the kernel detected *in_addr to
                        // not be equal to in_expected.
                        continue;

                    case EINTR:
                        // Interrupted. try again.
                        continue;

                    case ETIMEDOUT: MXL_TRACE("ETIMEDOUT. returning false"); break;

                    default:        break;
                }
                return false;
            }
        }
        return true;
    }

    template<typename T>
    bool waitUntilChanged(T const* in_addr, T in_expected, Duration in_timeout)
    {
        static_assert(valid_specialization_v<T>, "Only 32 bit types with natural alignment are supported.");

        return waitUntilChanged(in_addr, in_expected, currentTime(Clock::Realtime) + in_timeout);
    }

    template<typename T>
    void wakeOne(T const* in_addr)
    {
        static_assert(valid_specialization_v<T>, "Only 32 bit types with natural alignment are supported.");

        MXL_TRACE("Wake one waiting on = {}, val : {}", static_cast<void const*>(in_addr), *in_addr);
        do_wake_one(in_addr);
    }

    template<typename T>
    void wakeAll(T const* in_addr)
    {
        static_assert(valid_specialization_v<T>, "Only 32 bit types with natural alignment are supported.");

        MXL_TRACE("Wake all waiting on = {}, val : {}", static_cast<void const*>(in_addr), *in_addr);
        do_wake_all(in_addr);
    }

    // Explicit template instantiations
    template bool waitUntilChanged<std::uint32_t>(std::uint32_t const* in_addr, std::uint32_t in_expected, Timepoint in_deadline);
    template bool waitUntilChanged<std::uint32_t>(std::uint32_t const* in_addr, std::uint32_t in_expected, Duration in_timeout);
    template bool waitUntilChanged<std::int32_t>(std::int32_t const* in_addr, std::int32_t in_expected, Timepoint in_deadline);
    template bool waitUntilChanged<std::int32_t>(std::int32_t const* in_addr, std::int32_t in_expected, Duration in_timeout);
    template void wakeOne<std::uint32_t>(std::uint32_t const* in_addr);
    template void wakeOne<std::int32_t>(std::int32_t const* in_addr);
    template void wakeAll<std::uint32_t>(std::uint32_t const* in_addr);
    template void wakeAll<std::int32_t>(std::int32_t const* in_addr);
}
