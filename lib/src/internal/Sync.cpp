#include "Sync.hpp"
#include <cerrno>
#include <climits>
#include <cstdint>
#include <unistd.h>
#if defined(__linux__)
#   include <bits/time.h>
#   include <linux/futex.h>
#   include <sys/syscall.h>
#elif defined(__APPLE__)
#   include <os/os_sync_wait_on_address.h>
#endif
#include "Logging.hpp"

namespace mxl::lib
{
    namespace
    {
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
        static_assert(sizeof(T) == sizeof(std::uint32_t), "Only 32-bit types are supported.");

        while (true)
        {
            auto const now = currentTime(Clock::Realtime);
            if (now >= in_deadline)
            {
                MXL_DEBUG("Deadline already reached");
                return false; // Timeout
            }

            auto const ret = do_wait(in_addr, in_expected, in_deadline - now);
            if (ret == 0)
            {
                // TODO: This should probably be an atomic ...
                if (*in_addr != static_cast<T>(in_expected))
                {
                    return true;
                }
            }
            else
            {
                switch (errno)
                {
                    case EAGAIN:    MXL_TRACE("EAGAIN. returning true"); return true;

                    case ETIMEDOUT: MXL_TRACE("ETIMEDOUT. returning false"); return false;

                    case EINTR:
                        // Interrupted. try again.
                        continue;
                }

                return false;
            }
        }
    }

    template<typename T>
    bool waitUntilChanged(T const* in_addr, T in_expected, Duration in_timeout)
    {
        static_assert(sizeof(T) == sizeof(std::uint32_t), "Only 32-bit types are supported.");

        return waitUntilChanged(in_addr, in_expected, currentTime(Clock::Realtime) + in_timeout);
    }

    template<typename T>
    void wakeOne(T const* in_addr)
    {
        static_assert(sizeof(T) == sizeof(std::uint32_t), "Only 32-bit types are supported.");

        MXL_TRACE("Wake all waiting on = {}, val : {}", static_cast<void const*>(in_addr), *in_addr);
        do_wake_all(in_addr);
    }

    template<typename T>
    void wakeAll(T const* in_addr)
    {
        static_assert(sizeof(T) == sizeof(std::uint32_t), "Only 32-bit types are supported.");

        MXL_TRACE("Wake one waiting on = {}, val : {}", static_cast<void const*>(in_addr), *in_addr);
        do_wake_one(in_addr);
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
