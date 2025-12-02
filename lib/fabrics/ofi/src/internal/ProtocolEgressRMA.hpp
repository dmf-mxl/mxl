#pragma once

#include <rdma/fabric.h>
#include "DataLayout.hpp"
#include "Endpoint.hpp"
#include "Protocol.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    class RMAGrainEgressProtocol : public EgressProtocol
    {
    public:
        void transferGrain(Endpoint& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
            SliceRange const& sliceRange, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) override;

        void processCompletion(Completion::Data const&) override;

        bool hasPendingWork() const override;

        std::size_t destroy() override;

    private:
        friend class RMAGrainEgressProtocolTemplate;

        RMAGrainEgressProtocol(Completion::Token token, TargetInfo info, DataLayout dataLayout, std::vector<LocalRegion> _localRegions);

        Completion::Token _token;
        TargetInfo _remoteInfo;
        DataLayout _layout;
        std::vector<LocalRegion> _localRegions;
        std::size_t _pending = 0;
    };

    /** \brief Egress protocol for RMA writer endpoint.
     *
     * Handles transferring data to remote targets using remote write operations without bounce buffering.
     */
    class RMAGrainEgressProtocolTemplate final : public EgressProtocolTemplate
    {
    public:
        RMAGrainEgressProtocolTemplate(DataLayout layout, std::vector<Region> regions);

        std::vector<LocalRegion> registerMemory(std::shared_ptr<Domain> domain) override;
        std::unique_ptr<EgressProtocol> createInstance(Completion::Token, TargetInfo remoteInfo) override;

    private:
        DataLayout _dataLayout;
        std::vector<Region> _regions;
        std::optional<std::vector<LocalRegion>> _localRegions{};
    };

}
