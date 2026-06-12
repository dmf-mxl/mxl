#pragma once

#include "FabricInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    void requireCapability(FabricInfoView info, std::uint64_t cap, std::string_view message);
}
