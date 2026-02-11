// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file VariantUtils.hpp
 * @brief Template utilities for working with std::variant
 *
 * This file provides helper templates that simplify working with std::variant,
 * particularly the visitor pattern.
 */

#pragma once

namespace mxl::lib::fabrics::ofi
{
    /**
     * @struct overloaded
     * @brief Template struct for creating inline visitors for std::variant
     *
     * This is a C++17 pattern that allows you to write inline lambda visitors for std::variant
     * without having to create separate visitor classes.
     *
     * USAGE EXAMPLE:
     * ```cpp
     * std::variant<int, std::string> v = "hello";
     * std::visit(overloaded{
     *     [](int i) { std::cout << "Int: " << i; },
     *     [](std::string const& s) { std::cout << "String: " << s; }
     * }, v);
     * ```
     *
     * HOW IT WORKS:
     * - Takes a parameter pack of lambda types (Ts...)
     * - Inherits from all of them
     * - Uses "using Ts::operator()..." to bring all the call operators into scope
     * - This makes overloaded act as if it has all the call operators from all the lambdas
     *
     * USED IN MXL FABRICS:
     * - Completion::fid() uses this to get FID from either Data or Error variant
     * - Event processing uses this to handle different event types
     *
     * @tparam Ts Parameter pack of callable types (typically lambdas)
     */
    template<class... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...; ///< Bring all call operators from base classes into scope
    };
}
