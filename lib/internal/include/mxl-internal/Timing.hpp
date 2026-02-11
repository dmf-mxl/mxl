// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Timing.hpp
 * @brief Time primitives for MXL: Timepoint, Duration, and Clock
 *
 * MXL uses TAI (International Atomic Time) timestamps for all media timing, following
 * SMPTE ST 2059. TAI is:
 * - Monotonic (never jumps backward)
 * - Not affected by UTC leap seconds or NTP adjustments
 * - Synchronized across machines via PTP (Precision Time Protocol)
 *
 * Key design choices:
 * - Timepoint and Duration are opaque wrappers around int64_t nanoseconds for type safety
 * - All arithmetic operations are constexpr for compile-time evaluation where possible
 * - Conversions to/from POSIX timespec are provided for system API interop
 * - On systems without native CLOCK_TAI, we simulate it by adding leap seconds to CLOCK_REALTIME
 * - Negative time calculations clamp to zero (can't have negative absolute time)
 *
 * Why not std::chrono?
 * - We need explicit control over TAI vs UTC vs Monotonic clocks
 * - std::chrono's type system is complex and introduces conversion overhead
 * - Our timepoint/duration are POD types that can live in shared memory
 */

#pragma once

#include <cstdint>
#include <ctime>

namespace mxl::lib
{
    /**
     * An enumeration identifying one of the available system clocks.
     *
     * Clock::TAI is the primary clock for MXL media timestamps (SMPTE ST 2059).
     * Clock::Monotonic is used for relative timing (timeouts, rate limiting).
     * Clock::Realtime is generally avoided due to NTP adjustments.
     */
    enum class Clock
    {
        Monotonic,      // Monotonic time (unaffected by system time changes)
        Realtime,       // Wall-clock time (UTC, affected by NTP)
        TAI,            // International Atomic Time (for media timestamps)
        ProcessCPUTime, // CPU time consumed by this process
        ThreadCPUTime   // CPU time consumed by this thread
    };

    /**
     * Opaque structure representing a point in time as nanoseconds since an epoch.
     * The epoch depends on the Clock used to obtain the timepoint.
     * For Clock::TAI, the epoch is the SMPTE ST 2059 epoch (midnight 1970-01-01 TAI).
     */
    struct Timepoint;

    // Timepoint comparison operators (all constexpr for compile-time evaluation)
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

    /** Swap two timepoints (used by STL algorithms). */
    constexpr void swap(Timepoint& lhs, Timepoint& rhs) noexcept;

    /**
     * Get the current time from the specified clock.
     * For Clock::TAI, returns media-synchronized time (SMPTE ST 2059 epoch).
     * For Clock::Monotonic, returns monotonic time suitable for timeouts.
     *
     * @param clock Which system clock to query
     * @return The current time as a Timepoint
     */
    [[nodiscard]]
    Timepoint currentTime(Clock clock) noexcept;

    /**
     * Get the current UTC time.
     * Equivalent to currentTime(Clock::Realtime).
     * Generally avoided in MXL; prefer Clock::TAI for media timestamps.
     *
     * @return The current UTC time as a Timepoint
     */
    [[nodiscard]]
    Timepoint currentTimeUTC() noexcept;

    /**
     * Convert a POSIX timespec to a Timepoint.
     * Interprets timespec as nanoseconds since epoch (tv_sec * 1e9 + tv_nsec).
     * Used when interfacing with POSIX APIs like clock_gettime().
     *
     * @param timepoint POSIX timespec to convert
     * @return Equivalent Timepoint
     */
    [[nodiscard]]
    constexpr Timepoint asTimepoint(std::timespec const& timepoint) noexcept;

    /**
     * Convert a Timepoint to a POSIX timespec.
     * Breaks down nanoseconds into tv_sec and tv_nsec fields.
     * Used when calling POSIX APIs that expect timespec (e.g., futex, nanosleep).
     *
     * @param timepoint Timepoint to convert
     * @return Equivalent POSIX timespec
     */
    [[nodiscard]]
    constexpr std::timespec asTimeSpec(Timepoint timepoint) noexcept;

    /**
     * Opaque structure representing a duration (difference between two Timepoints).
     * Stored as int64_t nanoseconds. Can be positive (forward in time) or negative
     * (backward in time), though timepoint arithmetic clamps to prevent negative absolute times.
     */
    struct Duration;

    // Duration comparison operators (all constexpr)
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

    /** Swap two durations (used by STL algorithms). */
    constexpr void swap(Duration& lhs, Duration& rhs) noexcept;

    // Duration arithmetic operators
    [[nodiscard]]
    constexpr Duration operator+(Duration lhs, Duration rhs) noexcept;  // Add two durations
    [[nodiscard]]
    constexpr Duration operator-(Duration lhs, Duration rhs) noexcept;  // Subtract durations
    [[nodiscard]]
    constexpr Duration operator*(int lhs, Duration rhs) noexcept;       // Scale duration by integer
    [[nodiscard]]
    constexpr Duration operator*(Duration lhs, int rhs) noexcept;       // Scale duration by integer (commutative)
    [[nodiscard]]
    constexpr Duration operator/(Duration lhs, int rhs) noexcept;       // Divide duration by integer

    /**
     * Subtract two Timepoints to get the Duration between them.
     * Result can be negative if rhs > lhs.
     */
    [[nodiscard]]
    constexpr Duration operator-(Timepoint lhs, Timepoint rhs) noexcept;

    /**
     * Subtract a Duration from a Timepoint (move backward in time).
     * Result is clamped to zero if subtraction would yield negative time.
     */
    [[nodiscard]]
    constexpr Timepoint operator-(Timepoint lhs, Duration rhs) noexcept;

    /**
     * Add a Duration to a Timepoint (move forward in time).
     * Result is clamped to zero if addition would yield negative time.
     */
    [[nodiscard]]
    constexpr Timepoint operator+(Timepoint lhs, Duration rhs) noexcept;

    // Duration conversion functions (for human-readable units)

    /** Convert Duration to seconds (as floating-point for precision). */
    [[nodiscard]]
    constexpr double inSeconds(Duration duration) noexcept;

    /** Convert Duration to milliseconds. */
    [[nodiscard]]
    constexpr double inMilliSeconds(Duration duration) noexcept;

    /** Convert Duration to microseconds. */
    [[nodiscard]]
    constexpr double inMicroSeconds(Duration duration) noexcept;

    /** Convert Duration to nanoseconds. */
    [[nodiscard]]
    constexpr double inNanoSeconds(Duration duration) noexcept;

    /** Construct a Duration from seconds. */
    [[nodiscard]]
    constexpr Duration fromSeconds(double duration) noexcept;

    /** Construct a Duration from milliseconds. */
    [[nodiscard]]
    constexpr Duration fromMilliSeconds(double duration) noexcept;

    /** Construct a Duration from microseconds. */
    [[nodiscard]]
    constexpr Duration fromMicroSeconds(double duration) noexcept;

    /**
     * Convert a POSIX timespec to a Duration.
     * Interprets timespec as a duration (tv_sec * 1e9 + tv_nsec nanoseconds).
     */
    [[nodiscard]]
    constexpr Duration asDuration(std::timespec const& duration) noexcept;

    /**
     * Convert a Duration to a POSIX timespec.
     * Breaks down nanoseconds into tv_sec and tv_nsec fields.
     */
    [[nodiscard]]
    constexpr std::timespec asTimeSpec(Duration duration) noexcept;

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    /**
     * Timepoint implementation: simple wrapper around int64_t nanoseconds.
     * This is a POD type that can be stored in shared memory without concern
     * for C++ runtime complexities (vtables, RTTI, etc.).
     */
    struct Timepoint
    {
        using value_type = std::int64_t;

        value_type value;  // Nanoseconds since epoch (epoch depends on Clock used)

        /** Default constructor: initialize to zero (epoch). */
        constexpr Timepoint() noexcept;

        /** Construct from raw nanoseconds value. */
        constexpr explicit Timepoint(value_type value) noexcept;

        /**
         * Bool conversion: returns true if non-zero (non-epoch).
         * Useful for checking if a timepoint has been initialized.
         */
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
        // A zero timepoint is considered uninitialized/invalid
        return (value != 0);
    }

    // Timepoint comparison operators: simple integer comparison on nanosecond values
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

    // Convert POSIX timespec to Timepoint: combine seconds and nanoseconds
    constexpr Timepoint asTimepoint(std::timespec const& timepoint) noexcept
    {
        return Timepoint{(timepoint.tv_sec * 1'000'000'000LL) + timepoint.tv_nsec};
    }

    // Convert Timepoint to POSIX timespec: split nanoseconds into seconds and remainder
    constexpr std::timespec asTimeSpec(Timepoint timepoint) noexcept
    {
        return std::timespec{static_cast<std::time_t>(timepoint.value / 1'000'000'000LL), timepoint.value % 1'000'000'000LL};
    }

    /**
     * Duration implementation: simple wrapper around int64_t nanoseconds.
     * Like Timepoint, this is a POD type safe for shared memory.
     * Can be positive (forward in time) or negative (backward in time).
     */
    struct Duration
    {
        using value_type = std::int64_t;

        value_type value;  // Nanoseconds

        /** Default constructor: initialize to zero (no duration). */
        constexpr Duration() noexcept;

        /** Construct from raw nanoseconds value. */
        constexpr explicit Duration(value_type value) noexcept;

        /**
         * Bool conversion: returns true if non-zero.
         * Useful for checking if a duration is meaningful.
         */
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

    // Duration arithmetic: simple integer math on nanosecond values
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

    // Timepoint-Duration arithmetic

    /**
     * Subtract two Timepoints to get Duration.
     * Can return negative duration if rhs > lhs.
     */
    constexpr Duration operator-(Timepoint lhs, Timepoint rhs) noexcept
    {
        return Duration{lhs.value - rhs.value};
    }

    /**
     * Subtract Duration from Timepoint (move backward in time).
     * Clamps to zero to prevent negative absolute time (can't have timepoint < epoch).
     */
    constexpr Timepoint operator-(Timepoint lhs, Duration rhs) noexcept
    {
        auto const diff = lhs.value - rhs.value;
        return Timepoint{(diff >= 0) ? diff : 0LL};
    }

    /**
     * Add Duration to Timepoint (move forward in time).
     * Clamps to zero to prevent negative absolute time (defensive programming).
     */
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

    // Duration unit conversions (to human-readable values)

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

    // Duration constructors (from human-readable values)

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

    // POSIX timespec conversions for Duration

    constexpr Duration asDuration(std::timespec const& duration) noexcept
    {
        return Duration{(duration.tv_sec * 1'000'000'000LL) + duration.tv_nsec};
    }

    constexpr std::timespec asTimeSpec(Duration duration) noexcept
    {
        return std::timespec{static_cast<std::time_t>(duration.value / 1'000'000'000LL), duration.value % 1'000'000'000LL};
    }
}
