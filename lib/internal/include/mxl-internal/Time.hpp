// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Time.hpp
 * @brief Comparison operators for POSIX timespec structures
 *
 * This file provides relational operators for timespec (the POSIX struct with tv_sec and tv_nsec).
 * While MXL internally uses its own Timepoint/Duration types (see Timing.hpp), we occasionally
 * need to work with raw timespec values when interfacing with POSIX APIs like clock_gettime()
 * or futex wait operations.
 *
 * These operators enable natural comparisons like:
 *   if (deadline < currentTime) { ... }
 */

#pragma once

#include <ctime>

/** Test if two timespec values represent the same point in time. */
bool operator==(timespec const& lhs, timespec const& rhs);

/** Test if two timespec values represent different points in time. */
bool operator!=(timespec const& lhs, timespec const& rhs);

/** Test if lhs comes before rhs in time. */
bool operator<(timespec const& lhs, timespec const& rhs);

/** Test if lhs comes before or at the same time as rhs. */
bool operator<=(timespec const& lhs, timespec const& rhs);

/** Test if lhs comes after rhs in time. */
bool operator>(timespec const& lhs, timespec const& rhs);

/** Test if lhs comes after or at the same time as rhs. */
bool operator>=(timespec const& lhs, timespec const& rhs);
