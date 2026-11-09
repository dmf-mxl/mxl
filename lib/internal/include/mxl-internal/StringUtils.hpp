// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <charconv>
#include <concepts>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <fmt/format.h>

namespace mxl::lib
{
    /**
     * Convert a decimal string to the integer type T.
     * \param text The string to convert.
     * \param name The field name, used in the error message.
     * \return The value as T.
     * \throws std::invalid_argument If the string is not a decimal number in the range of T.
     */
    template<std::integral T>
    T stringToInteger(std::string_view text, std::string_view name)
    {
        auto value = T{};
        auto const* const first = text.data();
        auto const* const last = text.data() + text.size();
        auto const [end, error] = std::from_chars(first, last, value);
        if (error == std::errc::result_out_of_range)
        {
            throw std::invalid_argument{
                fmt::format("'{}' must be between {} and {}, got '{}'.", name, std::numeric_limits<T>::min(), std::numeric_limits<T>::max(), text)};
        }
        if ((error != std::errc{}) || (end != last))
        {
            throw std::invalid_argument{fmt::format("'{}' must be a decimal integer, got '{}'.", name, text)};
        }
        return value;
    }
}
