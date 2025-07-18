// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <ctime>

namespace mxl::lib
{
    /**
     * An enumeration identifying one of the available clocks in the system.
     */
    enum class Clock
    {
        Monotonic,
        Realtime,
        TAI,
        ProcessCPUTime,
        ThreadCPUTime
    };

    /** Opaque structure to store points in time. */
    struct Timepoint;

    [[nodiscard]]
    constexpr bool operator==(Timepoint lhs, Timepoint rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator!=(Timepoint lhs, Timepoint rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator<(Timepoint lhs, Timepoint rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator>(Timepoint lhs, Timepoint rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator<=(Timepoint lhs, Timepoint rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator>=(Timepoint lhs, Timepoint rhs) noexcept;

    constexpr void swap(Timepoint& lhs, Timepoint& rhs) noexcept;

    /**
     * Return the current time.
     * \return The current time as a Timepoint.
     */
    [[nodiscard]]
    Timepoint currentTime(Clock clock) noexcept;

    /**
     * Return the current time in UTC.
     * \return The current time as a Timepoint in UTC.
     */
    [[nodiscard]]
    Timepoint currentTimeUTC() noexcept;

    /**
     * Return the equivalent of a timespec instance relative to whichever
     * reference frame the system uses as a time point.
     * \return The equivalent of a timespec instance as a time point.
     */
    [[nodiscard]]
    constexpr Timepoint asTimepoint(std::timespec const& timepoint) noexcept;

    /**
     * Return the equivalent of a time point as a timespec instance
     * relative to whichever reference frame the system uses.
     * \return The equivalent of a time point as a timespec instance.
     */
    [[nodiscard]]
    constexpr std::timespec asTimeSpec(Timepoint timepoint) noexcept;

    /** Opaque structure to durations points in time. */
    struct Duration;

    [[nodiscard]]
    constexpr bool operator==(Duration lhs, Duration rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator!=(Duration lhs, Duration rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator<(Duration lhs, Duration rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator>(Duration lhs, Duration rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator<=(Duration lhs, Duration rhs) noexcept;
    [[nodiscard]]
    constexpr bool operator>=(Duration lhs, Duration rhs) noexcept;

    constexpr void swap(Duration& lhs, Duration& rhs) noexcept;

    [[nodiscard]]
    constexpr Duration operator+(Duration lhs, Duration rhs) noexcept;
    [[nodiscard]]
    constexpr Duration operator-(Duration lhs, Duration rhs) noexcept;
    [[nodiscard]]
    constexpr Duration operator*(int lhs, Duration rhs) noexcept;
    [[nodiscard]]
    constexpr Duration operator*(Duration lhs, int rhs) noexcept;
    [[nodiscard]]
    constexpr Duration operator/(Duration lhs, int rhs) noexcept;

    [[nodiscard]]
    constexpr Duration operator-(Timepoint lhs, Timepoint rhs) noexcept;
    [[nodiscard]]
    constexpr Timepoint operator-(Timepoint lhs, Duration rhs) noexcept;
    [[nodiscard]]
    constexpr Timepoint operator+(Timepoint lhs, Duration rhs) noexcept;

    /**
     * Return the equivalent of a duration in seconds.
     * \return The equivalent of a duration in seconds.
     */
    [[nodiscard]]
    constexpr double inSeconds(Duration duration) noexcept;

    /**
     * Return the equivalent of a duration in milliseconds.
     * \return The equivalent of a duration in milliseconds.
     */
    [[nodiscard]]
    constexpr double inMilliSeconds(Duration duration) noexcept;

    /**
     * Return the equivalent of a duration in microseconds.
     * \return The equivalent of a duration in microseconds.
     */
    [[nodiscard]]
    constexpr double inMicroSeconds(Duration duration) noexcept;

    /**
     * Return the equivalent of a duration in nanoseconds.
     * \return The equivalent of a duration in nanoseconds.
     */
    [[nodiscard]]
    constexpr double inNanoSeconds(Duration duration) noexcept;

    /**
     * Return a duration representing the specified number of seconds.
     * \return A duration representing the specified number of seconds.
     */
    [[nodiscard]]
    constexpr Duration fromSeconds(double duration) noexcept;

    /**
     * Return a duration representing the specified number of milliseconds.
     * \return A duration representing the specified number of milliseconds.
     */
    [[nodiscard]]
    constexpr Duration fromMilliSeconds(double duration) noexcept;

    /**
     * Return a duration representing the specified number of microseconds.
     * \return A duration representing the specified number of milcroliseconds.
     */
    [[nodiscard]]
    constexpr Duration fromMicroSeconds(double duration) noexcept;

    /**
     * Return the equivalent of a timespec instance as a duration.
     * \return The equivalent of a timespec instance as a duration.
     */
    [[nodiscard]]
    constexpr Duration asDuration(std::timespec const& duration) noexcept;

    /**
     * Return the equivalent of a duration as a timespec instance.
     * \return The equivalent of a duration as a timespec instance.
     */
    [[nodiscard]]
    constexpr std::timespec asTimeSpec(Duration duration) noexcept;

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    struct Timepoint
    {
        using value_type = std::int64_t;

        value_type value;

        constexpr Timepoint() noexcept;
        constexpr explicit Timepoint(value_type value) noexcept;

        constexpr explicit operator bool() const noexcept;
    };

    constexpr Timepoint::Timepoint() noexcept
        : value{}
    {}

    constexpr Timepoint::Timepoint(value_type value) noexcept
        : value{value}
    {}

    constexpr Timepoint::operator bool() const noexcept
    {
        return (value != 0);
    }

    constexpr bool operator==(Timepoint lhs, Timepoint rhs) noexcept
    {
        return (lhs.value == rhs.value);
    }

    constexpr bool operator!=(Timepoint lhs, Timepoint rhs) noexcept
    {
        return (lhs.value != rhs.value);
    }

    constexpr bool operator<(Timepoint lhs, Timepoint rhs) noexcept
    {
        return (lhs.value < rhs.value);
    }

    constexpr bool operator>(Timepoint lhs, Timepoint rhs) noexcept
    {
        return (lhs.value > rhs.value);
    }

    constexpr bool operator<=(Timepoint lhs, Timepoint rhs) noexcept
    {
        return (lhs.value <= rhs.value);
    }

    constexpr bool operator>=(Timepoint lhs, Timepoint rhs) noexcept
    {
        return (lhs.value >= rhs.value);
    }

    constexpr void swap(Timepoint& lhs, Timepoint& rhs) noexcept
    {
        auto const temp = lhs.value;
        lhs.value = rhs.value;
        rhs.value = temp;
    }

    constexpr Timepoint asTimepoint(std::timespec const& timepoint) noexcept
    {
        return Timepoint{(timepoint.tv_sec * 1'000'000'000LL) + timepoint.tv_nsec};
    }

    constexpr std::timespec asTimeSpec(Timepoint timepoint) noexcept
    {
        return std::timespec{static_cast<std::time_t>(timepoint.value / 1'000'000'000LL), timepoint.value % 1'000'000'000LL};
    }

    struct Duration
    {
        using value_type = std::int64_t;

        value_type value;

        constexpr Duration() noexcept;
        constexpr explicit Duration(value_type value) noexcept;

        constexpr explicit operator bool() const noexcept;
    };

    constexpr Duration::Duration() noexcept
        : value{}
    {}

    constexpr Duration::Duration(value_type value) noexcept
        : value{value}
    {}

    constexpr Duration::operator bool() const noexcept
    {
        return (value != 0);
    }

    constexpr bool operator==(Duration lhs, Duration rhs) noexcept
    {
        return (lhs.value == rhs.value);
    }

    constexpr bool operator!=(Duration lhs, Duration rhs) noexcept
    {
        return (lhs.value != rhs.value);
    }

    constexpr bool operator<=(Duration lhs, Duration rhs) noexcept
    {
        return (lhs.value <= rhs.value);
    }

    constexpr bool operator>=(Duration lhs, Duration rhs) noexcept
    {
        return (lhs.value >= rhs.value);
    }

    constexpr bool operator<(Duration lhs, Duration rhs) noexcept
    {
        return (lhs.value < rhs.value);
    }

    constexpr bool operator>(Duration lhs, Duration rhs) noexcept
    {
        return (lhs.value > rhs.value);
    }

    constexpr Duration operator+(Duration lhs, Duration rhs) noexcept
    {
        return Duration{lhs.value + rhs.value};
    }

    constexpr Duration operator-(Duration lhs, Duration rhs) noexcept
    {
        return Duration{lhs.value - rhs.value};
    }

    constexpr Duration operator*(int lhs, Duration rhs) noexcept
    {
        return Duration{lhs * rhs.value};
    }

    constexpr Duration operator*(Duration lhs, int rhs) noexcept
    {
        return Duration{lhs.value * rhs};
    }

    constexpr Duration operator/(Duration lhs, int rhs) noexcept
    {
        return Duration{lhs.value / rhs};
    }

    constexpr Duration operator-(Timepoint lhs, Timepoint rhs) noexcept
    {
        return Duration{lhs.value - rhs.value};
    }

    constexpr Timepoint operator-(Timepoint lhs, Duration rhs) noexcept
    {
        auto const diff = lhs.value - rhs.value;
        return Timepoint{(diff >= 0) ? diff : 0LL};
    }

    constexpr Timepoint operator+(Timepoint lhs, Duration rhs) noexcept
    {
        auto const sum = lhs.value + rhs.value;
        return Timepoint{(sum >= 0) ? sum : 0LL};
    }

    constexpr void swap(Duration& lhs, Duration& rhs) noexcept
    {
        auto const temp = lhs.value;
        lhs.value = rhs.value;
        rhs.value = temp;
    }

    constexpr double inSeconds(Duration duration) noexcept
    {
        return static_cast<double>(duration.value) / 1'000'000'000.0;
    }

    constexpr double inMilliSeconds(Duration duration) noexcept
    {
        return static_cast<double>(duration.value) / 1'000'000.0;
    }

    constexpr double inMicroSeconds(Duration duration) noexcept
    {
        return static_cast<double>(duration.value) / 1'000.0;
    }

    constexpr double inNanoSeconds(Duration duration) noexcept
    {
        return static_cast<double>(duration.value);
    }

    constexpr Duration fromSeconds(double duration) noexcept
    {
        return Duration{static_cast<Duration::value_type>(duration * 1'000'000'000.0)};
    }

    constexpr Duration fromMilliSeconds(double duration) noexcept
    {
        return Duration{static_cast<Duration::value_type>(duration * 1'000'000.0)};
    }

    constexpr Duration fromMicroSeconds(double duration) noexcept
    {
        return Duration{static_cast<Duration::value_type>(duration * 1'000.0)};
    }

    constexpr Duration asDuration(std::timespec const& duration) noexcept
    {
        return Duration{(duration.tv_sec * 1'000'000'000LL) + duration.tv_nsec};
    }

    constexpr std::timespec asTimeSpec(Duration duration) noexcept
    {
        return std::timespec{static_cast<std::time_t>(duration.value / 1'000'000'000LL), duration.value % 1'000'000'000LL};
    }
}