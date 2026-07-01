// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <optional>
#include "FabricInterfaceList.hpp"
#include "ProviderConfig.hpp"

namespace mxl::lib::fabrics::ofi
{
    /**
     * Query path: enumerate all available fabric interfaces matching the optional filter criteria.
     * Capabilities in the query are ignored — all matching interfaces are returned with their
     * reported capabilities as informational output. When \p query is nullopt, all interfaces
     * across all providers are returned.
     */
    FabricInterfaceList probeInterfaces(std::optional<std::reference_wrapper<::mxlFabricsInterfaceConfig const>> query);

    /**
     * Setup path: select a single source interface for target or initiator setup.
     * Capabilities from \p interfaceConfig are treated as requirements — if no transfer capability
     * (REMOTE_WRITE or SEND_RECEIVE) is set, REMOTE_WRITE is applied with a warning. Returns the
     * matched fabric info and its resolved provider configuration.
     * \param isTarget true for target (FI_REMOTE_WRITE), false for initiator (FI_WRITE).
     * \throws Exception::noFabric if no matching interface is found or capabilities are unsupported.
     */
    std::pair<FabricInfo, ProviderConfig> selectSourceInterface(::mxlFabricsInterfaceConfig const& interfaceConfig, bool isTarget);
}
