// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mxl/rational.h>

constexpr bool isValid(mxlRational const& rational) noexcept
{
    return (rational.denominator != 0);
}

constexpr bool operator==(mxlRational const& lhs, mxlRational const& rhs) noexcept
{
    return (lhs.numerator * rhs.denominator) == (lhs.denominator * rhs.numerator);
}

constexpr bool operator!=(mxlRational const& lhs, mxlRational const& rhs) noexcept
{
    return !(lhs == rhs);
}
