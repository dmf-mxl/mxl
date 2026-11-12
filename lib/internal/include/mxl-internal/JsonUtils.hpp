// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <fmt/format.h>
#include <picojson/wrapper.h>
#include "StringUtils.hpp"

namespace mxl::lib
{
    /// Limits applied by parseJson().
    struct JsonLimits
    {
        /// Largest accepted input in bytes. This also bounds the length of any string in the document.
        std::size_t maxInputSize = std::size_t{64} * 1024U;
        /// Largest number of nested arrays and objects. picojson parses recursively, so this also bounds stack use.
        std::size_t maxDepth = 32U;
        /// Largest number of items in one array or members in one object.
        std::size_t maxContainerSize = 1024U;
    };

    namespace detail
    {
        /// picojson parse context that builds a value like picojson::default_parse_context and stops when a limit in
        /// JsonLimits is exceeded or an object has a duplicate key. The reason is stored in the shared error string.
        class LimitedJsonParseContext
        {
        public:
            LimitedJsonParseContext(picojson::value* out, JsonLimits const& limits, std::string& error, std::size_t depth) noexcept
                : _out{out}
                , _limits{&limits}
                , _error{&error}
                , _depth{depth}
            {}

            LimitedJsonParseContext(LimitedJsonParseContext const&) = delete;
            LimitedJsonParseContext& operator=(LimitedJsonParseContext const&) = delete;

            bool set_null()
            {
                *_out = picojson::value{};
                return true;
            }

            bool set_bool(bool value)
            {
                *_out = picojson::value{value};
                return true;
            }

#ifdef PICOJSON_USE_INT64
            bool set_int64(std::int64_t value)
            {
                *_out = picojson::value{value};
                return true;
            }
#endif

            bool set_number(double value)
            {
                // picojson::value throws std::overflow_error for these. Report them as a parse error instead.
                if (!std::isfinite(value))
                {
                    *_error = "JSON number is out of range.";
                    return false;
                }
                *_out = picojson::value{value};
                return true;
            }

            template<typename Iter>
            bool parse_string(picojson::input<Iter>& in)
            {
                *_out = picojson::value{picojson::string_type, false};
                return picojson::_parse_string(_out->get<std::string>(), in);
            }

            bool parse_array_start()
            {
                if (!checkDepth())
                {
                    return false;
                }
                *_out = picojson::value{picojson::array_type, false};
                return true;
            }

            template<typename Iter>
            bool parse_array_item(picojson::input<Iter>& in, std::size_t index)
            {
                if (index >= _limits->maxContainerSize)
                {
                    *_error = fmt::format("JSON array has more than {} items.", _limits->maxContainerSize);
                    return false;
                }
                auto& array = _out->get<picojson::array>();
                array.emplace_back();
                auto child = LimitedJsonParseContext{&array.back(), *_limits, *_error, _depth + 1U};
                return picojson::_parse(child, in);
            }

            bool parse_array_stop(std::size_t)
            {
                return true;
            }

            bool parse_object_start()
            {
                if (!checkDepth())
                {
                    return false;
                }
                *_out = picojson::value{picojson::object_type, false};
                return true;
            }

            template<typename Iter>
            bool parse_object_item(picojson::input<Iter>& in, std::string const& key)
            {
                auto& object = _out->get<picojson::object>();
                if (object.size() >= _limits->maxContainerSize)
                {
                    *_error = fmt::format("JSON object has more than {} members.", _limits->maxContainerSize);
                    return false;
                }
                // picojson keeps the last of several equal keys. Reject them, because other parsers may keep the first.
                auto const [it, inserted] = object.try_emplace(key);
                if (!inserted)
                {
                    *_error = fmt::format("JSON object has the key '{}' more than once.", key);
                    return false;
                }
                auto child = LimitedJsonParseContext{&it->second, *_limits, *_error, _depth + 1U};
                return picojson::_parse(child, in);
            }

        private:
            bool checkDepth()
            {
                if (_depth >= _limits->maxDepth)
                {
                    *_error = fmt::format("JSON nesting is deeper than {} levels.", _limits->maxDepth);
                    return false;
                }
                return true;
            }

        private:
            picojson::value* _out;
            JsonLimits const* _limits;
            std::string* _error;
            /// Number of arrays and objects that enclose the value this context parses.
            std::size_t _depth;
        };
    }

    /**
     * Parse a JSON document with the limits in \p limits.
     *
     * Unlike picojson::parse(value&, std::string const&), this rejects any content after the first value other than
     * whitespace, and rejects objects with duplicate keys.
     *
     * \param out Receives the parsed value. Its content is unspecified on error.
     * \param text The JSON document.
     * \param limits The limits to apply.
     * \return An empty string on success, otherwise a description of the error.
     */
    inline std::string parseJson(picojson::value& out, std::string_view text, JsonLimits const& limits = {})
    {
        if (text.size() > limits.maxInputSize)
        {
            return fmt::format("JSON input has {} bytes, the limit is {}.", text.size(), limits.maxInputSize);
        }

        auto limitError = std::string{};
        auto syntaxError = std::string{};
        auto ctx = detail::LimitedJsonParseContext{&out, limits, limitError, 0U};
        auto const end = picojson::_parse(ctx, text.begin(), text.end(), &syntaxError);
        if (!limitError.empty())
        {
            return limitError;
        }
        if (!syntaxError.empty())
        {
            return syntaxError;
        }

        auto const isWhitespace = [](char c) noexcept
        {
            return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r');
        };
        if (!std::all_of(end, text.end(), isWhitespace))
        {
            return "Unexpected content after the JSON value.";
        }
        return {};
    }

    /**
     * Convert a JSON number to the integer type T.
     *
     * Converting a double that is outside the range of an integer type is undefined behavior in C++, so the value is
     * checked before the conversion.
     *
     * \param value The JSON value.
     * \param name The field name, used in the error message.
     * \return The value as T.
     * \throws std::invalid_argument If the value is not a number, not a whole number, or outside the range of T.
     */
    template<std::integral T>
    inline T jsonToInteger(picojson::value const& value, std::string_view name)
    {
        if (!value.is<double>())
        {
            throw std::invalid_argument{fmt::format("'{}' must be a number.", name)};
        }

        auto const number = value.get<double>();

        // 2^digits is the first value past the range of T. It is exact as a double, while numeric_limits<T>::max() may
        // round up to it.
        auto const upper = std::ldexp(1.0, std::numeric_limits<T>::digits);
        auto const lower = std::numeric_limits<T>::is_signed ? -upper : 0.0;
        if ((std::trunc(number) != number) || (number < lower) || (number >= upper))
        {
            throw std::invalid_argument{fmt::format(
                "'{}' must be a whole number between {} and {}, got {}.", name, std::numeric_limits<T>::min(), std::numeric_limits<T>::max(), number)};
        }
        return static_cast<T>(number);
    }

    /**
     * Convert a JSON string holding a decimal number to the integer type T.
     *
     * Use this for values such as memory addresses and keys, which are stored as strings because a JSON number is a
     * double and cannot hold every 64-bit integer exactly.
     *
     * \param value The JSON value.
     * \param name The field name, used in the error message.
     * \return The value as T.
     * \throws std::invalid_argument If the value is not a string, or not a decimal number in the range of T.
     * \see stringToInteger()
     */
    template<std::integral T>
    inline T jsonStringToInteger(picojson::value const& value, std::string_view name)
    {
        if (!value.is<std::string>())
        {
            throw std::invalid_argument{fmt::format("'{}' must be a string.", name)};
        }
        return stringToInteger<T>(value.get<std::string>(), name);
    }
}
