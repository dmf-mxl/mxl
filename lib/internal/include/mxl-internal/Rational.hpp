// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Rational.hpp
 * @brief Utility functions for working with mxlRational (frame rates, aspect ratios)
 *
 * Rational numbers are used throughout MXL to represent frame rates (e.g., 24000/1001 for 23.976fps)
 * and aspect ratios with exact precision. This avoids floating-point rounding errors in timing calculations.
 *
 * Key concepts:
 * - A rational is valid if its denominator is non-zero
 * - Equality is tested via cross-multiplication to avoid division and handle non-reduced fractions
 * - All operations are constexpr for compile-time evaluation where possible
 */

#pragma once

#include <mxl/rational.h>

/**
 * Check if a rational number is valid (has non-zero denominator).
 * An invalid rational (denominator == 0) represents undefined/uninitialized values.
 *
 * @param rational The rational number to validate
 * @return true if denominator is non-zero, false otherwise
 */
constexpr bool isValid(mxlRational const& rational) noexcept
{
    return (rational.denominator != 0);
}

/**
 * Test equality of two rational numbers using cross-multiplication.
 * This approach:
 * - Avoids division (which could introduce rounding errors)
 * - Works correctly even if fractions aren't in lowest terms (e.g., 2/4 == 1/2)
 * - Is constexpr for compile-time evaluation
 *
 * @param lhs Left-hand rational
 * @param rhs Right-hand rational
 * @return true if the rationals represent the same value
 */
constexpr bool operator==(mxlRational const& lhs, mxlRational const& rhs) noexcept
{
    return (lhs.numerator * rhs.denominator) == (lhs.denominator * rhs.numerator);
}

/**
 * Test inequality of two rational numbers.
 *
 * @param lhs Left-hand rational
 * @param rhs Right-hand rational
 * @return true if the rationals represent different values
 */
constexpr bool operator!=(mxlRational const& lhs, mxlRational const& rhs) noexcept
{
    return !(lhs == rhs);
}
