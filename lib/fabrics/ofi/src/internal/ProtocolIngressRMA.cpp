// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "ProtocolIngressRMA.hpp"
#include "Exception.hpp"
#include "ImmData.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    RMAGrainIngressProtocol::RMAGrainIngressProtocol(std::vector<Region> regions)
        : _regions(std::move(regions))
    {}

    std::vector<RemoteRegion> RMAGrainIngressProtocol::registerMemory(std::shared_ptr<Domain> domain)
    {
        if (_isMemoryRegistered)
        {
            throw Exception::invalidState("Memory is already registered.");
        }

        domain->registerRegions(_regions, FI_REMOTE_WRITE);
        return domain->remoteRegions();
    }

    void RMAGrainIngressProtocol::start(Endpoint& endpoint)
    {
        if (endpoint.domain()->usingRecvBufForCqData())
        {
            endpoint.recv(immDataRegion());
        }
    }

    std::optional<Target::GrainReadResult> RMAGrainIngressProtocol::readGrain(Endpoint& endpoint, Completion const& completion)
    {
        auto completionData = completion.tryData();
        if (!completionData)
        {
            return {};
        }

        if (_immDataBuffer)
        {
            endpoint.recv(_immDataBuffer->toLocalRegion());
        }

        auto immData = completionData->data();
        if (!immData)
        {
            throw Exception::invalidState("Received a completion without immediate data.");
        }

        auto [slot, slice] = ImmDataGrain{static_cast<std::uint32_t>(*immData)}.unpack();
        auto grainIndex = getGrainIndexInRingSlot(_regions, slot);
        return std::make_optional<Target::GrainReadResult>(grainIndex, slice);
    }

    void RMAGrainIngressProtocol::reset()
    {}

    LocalRegion RMAGrainIngressProtocol::immDataRegion()
    {
        if (!_immDataBuffer)
        {
            _immDataBuffer.emplace();
        }

        return _immDataBuffer->toLocalRegion();
    }

}
