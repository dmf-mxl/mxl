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

        // Set the number of valid slices in the grain header. This information is received through the immediate data and must be updated
        // in the local shared memory in the case of partial writes.
        setValidSlicesForGrain(_regions, slot, slice);

        // Get the actual grain index from the grain header in share memory. This was written in the first RMA write.
        auto grainIndex = getGrainIndexInRingSlot(_regions, slot);

        return std::make_optional<Target::GrainReadResult>(grainIndex);
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
