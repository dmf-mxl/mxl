// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "FabricInfoHelpers.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{
    void requireCapability(FabricInfoView info, std::uint64_t cap, std::string_view message)
    {
        if ((info->caps & cap) != cap)
        {
            throw Exception::unsupportedOperation("{}", message);
        }
    }
}
