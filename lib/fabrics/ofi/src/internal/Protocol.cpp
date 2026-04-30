// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Protocol.hpp"
#include <cassert>
#include "Exception.hpp"
#include "ProtocolEgressRMA.hpp"
#include "ProtocolIngressRMA.hpp"

namespace mxl::lib::fabrics::ofi
{
    std::unique_ptr<IngressProtocol> selectIngressProtocol(DataLayout const& layout, std::vector<Region> regions, std::uint32_t maxSyncBatchSize)
    {
        if (layout.isVideo())
        {
            return std::make_unique<RMAGrainIngressProtocol>(std::move(regions));
        }
        else if (layout.isAudio())
        {
            if (regions.size() != 1)
            {
                throw Exception::invalidArgument("Expected exactly 1 region for sample protocol.");
            }
            return std::make_unique<RMASampleIngressProtocol>(regions.front(), layout.asAudio(), maxSyncBatchSize);
        }
        else
        {
            throw Exception::invalidArgument("Unsupported data layout");
        }
    }

    std::unique_ptr<EgressProtocolTemplate> selectEgressProtocol(DataLayout const& layout, std::vector<Region> regions)
    {
        if (!layout.isVideo())
        {
            return std::make_unique<RMAGrainEgressProtocolTemplate>(layout.asVideo(), std::move(regions));
        }
        else if (layout.isAudio())
        {
            if (regions.size() != 1)
            {
                throw Exception::invalidArgument("Expected exactly 1 region for sample protocol.");
            }
            return std::make_unique<RMASampleEgressProtocolTemplate>(layout.asAudio(), regions.front());
        }
        else
        {
            throw Exception::invalidArgument("Unsupported data layout");
        }
    }

}
