#pragma once

#include <rdma/fabric.h>
#include "Endpoint.hpp"
#include "Protocol.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief Egress protocol for RMA writer endpoint.
     *
     * Handles transferring data to remote targets using remote write operations without bounce buffering.
     */
    class EgressProtocolWriter final : public EgressProtocol
    {
    public:
        /**
         * \brief Construct a new EgressProtocolWriter object
         * \param ep The endpoint to use for transfers
         * \param remoteRegions The remote memory regions to transfer to
         * \param layout The video data layout describing the memory region
         * \note Passed references should live at least as long as this instance
         */
        EgressProtocolWriter(std::shared_ptr<Endpoint> ep, std::vector<RemoteRegion> const& remoteRegions, DataLayout::VideoDataLayout const& layout);

        /** \copydoc Protocol::transferGrain() */
        std::size_t transferGrain(LocalRegion const& localRegion, std::uint64_t remoteIndex, std::uint32_t remotePayloadOffset,
            SliceRange const& sliceRange, ::fi_addr_t destAddr) override;

    private:
        std::shared_ptr<Endpoint> _ep;
        std::vector<RemoteRegion> const& _remoteRegions;
        DataLayout::VideoDataLayout const& _layout;
    };
}
