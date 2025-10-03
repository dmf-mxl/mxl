// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace mxl::lib::fabrics::ofi
{
    // Allows us to use the visitor pattern with lambdas
    template<class... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };
}
