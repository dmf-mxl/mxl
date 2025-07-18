// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mxl/rational.h>

constexpr bool isValid(Rational const& rational) noexcept
{
    return (rational.denominator != 0);
}

constexpr bool operator==(Rational const& lhs, Rational const& rhs) noexcept
{
    return (lhs.numerator * rhs.denominator) == (lhs.denominator * rhs.numerator);
}

constexpr bool operator!=(Rational const& lhs, Rational const& rhs) noexcept
{
    return !(lhs == rhs);
}
