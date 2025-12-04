// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Protocol.hpp"
#include <cassert>
#include "Exception.hpp"
#include "ProtocolEgressRMA.hpp"
#include "ProtocolIngressRMA.hpp"

namespace mxl::lib::fabrics::ofi
{
    std::unique_ptr<IngressProtocol> selectIngressProtocol(DataLayout const& layout, std::vector<Region> regions)
    {
        if (!layout.isVideo())
        {
            throw Exception::internal("Only grain transport supported for now.");
        }

        return std::make_unique<RMAGrainIngressProtocol>(std::move(regions));
    }

    std::unique_ptr<EgressProtocolTemplate> selectEgressProtocol(DataLayout const& layout, std::vector<Region> regions)
    {
        if (!layout.isVideo())
        {
            throw Exception::internal("Only grain transport supported for now.");
        }

        return std::make_unique<RMAGrainEgressProtocolTemplate>(layout, std::move(regions));
    }

}
