#pragma once

#include <rdma/fabric.h>
#include "DataLayout.hpp"
#include "Endpoint.hpp"
#include "Protocol.hpp"

namespace mxl::lib::fabrics::ofi
{
    class RMAGrainEgressProtocol : public EgressProtocol
    {
    public:
        /** \copydoc EgressProtocol::transferGrain()
         */
        void transferGrain(Endpoint& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
            SliceRange const& sliceRange, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) override;

        /** \copydoc EgressProtocol::processCompletion()
         */
        void processCompletion(Completion::Data const&) override;

        /** \copydoc EgressProtocol::hasPendingWork()
         */
        [[nodiscard]]
        bool hasPendingWork() const override;

        /** \copydoc EgressProtocol::destroy()
         */
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

        void registerMemory(std::shared_ptr<Domain> domain) override;
        std::unique_ptr<EgressProtocol> createInstance(Completion::Token, TargetInfo remoteInfo) override;

    private:
        DataLayout _dataLayout;
        std::vector<Region> _regions;
        std::optional<std::vector<LocalRegion>> _localRegions{};
    };

}
