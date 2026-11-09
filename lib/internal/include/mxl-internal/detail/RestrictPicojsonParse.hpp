// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus

#   include <iosfwd>
#   include <string>

#   define MXL_PICOJSON_PARSE_DEPRECATED [[deprecated("Use mxl::lib::parseJson() from mxl-internal/JsonUtils.hpp, which applies input limits.")]]

namespace picojson
{
    class value;

    template<typename Iter>
    MXL_PICOJSON_PARSE_DEPRECATED inline std::string parse(value& out, Iter& pos, Iter const& last);

    template<typename Iter>
    MXL_PICOJSON_PARSE_DEPRECATED inline Iter parse(value& out, Iter const& first, Iter const& last, std::string* err);

    MXL_PICOJSON_PARSE_DEPRECATED inline std::string parse(value& out, std::string const& s);

    MXL_PICOJSON_PARSE_DEPRECATED inline std::string parse(value& out, std::istream& is);
}

MXL_PICOJSON_PARSE_DEPRECATED inline std::istream& operator>>(std::istream& is, picojson::value& x);

#   undef MXL_PICOJSON_PARSE_DEPRECATED

#endif
