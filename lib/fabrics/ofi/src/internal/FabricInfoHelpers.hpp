// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "FabricInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    void requireCapability(FabricInfoView info, std::uint64_t cap, std::string_view message);
}
