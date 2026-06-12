#pragma once

#include <functional>
#include <optional>
#include "FabricInterfaceList.hpp"

namespace mxl::lib::fabrics::ofi
{
    FabricInterfaceList probeInterfaces(std::optional<std::reference_wrapper<::mxlFabricsInterfaceConfig const>> query);
    std::pair<FabricInfo, ProviderConfig> selectSourceInterface(::mxlFabricsInterfaceConfig const& interfaceConfig, bool isTarget);
}
