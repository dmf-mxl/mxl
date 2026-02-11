// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Time.cpp
 * @brief Implements comparison operators for POSIX timespec structures
 *
 * The POSIX timespec structure represents time with nanosecond precision as:
 *   struct timespec {
 *       time_t tv_sec;   // seconds
 *       long   tv_nsec;  // nanoseconds [0, 999999999]
 *   };
 *
 * MXL uses TAI (International Atomic Time) timestamps throughout for precise
 * synchronization of media streams. These comparison operators enable timespec
 * values to be used with standard algorithms and deadlines.
 *
 * Design notes:
 * - All operators are defined in terms of < and == for consistency
 * - Comparisons handle the two-component nature of timespec correctly
 * - These are global operators to work with existing timespec definitions
 */

#include "mxl-internal/Time.hpp"
#include <ctime>

/**
 * @brief Test equality of two timespec values
 *
 * Two timespec values are equal if both their seconds and nanoseconds match.
 *
 * @param lhs Left-hand side timespec
 * @param rhs Right-hand side timespec
 * @return true if both seconds and nanoseconds are equal
 */
bool operator==(timespec const& lhs, timespec const& rhs)
{
    return (lhs.tv_sec == rhs.tv_sec) && (lhs.tv_nsec == rhs.tv_nsec);
}

/**
 * @brief Test inequality of two timespec values
 *
 * @param lhs Left-hand side timespec
 * @param rhs Right-hand side timespec
 * @return true if either seconds or nanoseconds differ
 */
bool operator!=(timespec const& lhs, timespec const& rhs)
{
    return !(lhs == rhs);
}

/**
 * @brief Test if one timespec is less than another
 *
 * Comparison is performed hierarchically:
 * 1. First compare seconds
 * 2. If seconds are equal, compare nanoseconds
 *
 * This correctly handles the two-component representation.
 *
 * @param lhs Left-hand side timespec
 * @param rhs Right-hand side timespec
 * @return true if lhs represents an earlier time than rhs
 */
bool operator<(timespec const& lhs, timespec const& rhs)
{
    // Compare seconds first
    if (lhs.tv_sec < rhs.tv_sec)
    {
        return true;
    }
    if (lhs.tv_sec > rhs.tv_sec)
    {
        return false;
    }

    // Seconds are equal, compare nanoseconds
    return lhs.tv_nsec < rhs.tv_nsec;
}

/**
 * @brief Test if one timespec is less than or equal to another
 *
 * @param lhs Left-hand side timespec
 * @param rhs Right-hand side timespec
 * @return true if lhs is earlier than or equal to rhs
 */
bool operator<=(timespec const& lhs, timespec const& rhs)
{
    return (lhs < rhs) || (lhs == rhs);
}

/**
 * @brief Test if one timespec is greater than another
 *
 * @param lhs Left-hand side timespec
 * @param rhs Right-hand side timespec
 * @return true if lhs represents a later time than rhs
 */
bool operator>(timespec const& lhs, timespec const& rhs)
{
    return !(lhs <= rhs);
}

/**
 * @brief Test if one timespec is greater than or equal to another
 *
 * @param lhs Left-hand side timespec
 * @param rhs Right-hand side timespec
 * @return true if lhs is later than or equal to rhs
 */
bool operator>=(timespec const& lhs, timespec const& rhs)
{
    return !(lhs < rhs);
}
