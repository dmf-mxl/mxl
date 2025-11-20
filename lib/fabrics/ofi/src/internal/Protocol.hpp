#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <rdma/fabric.h>
#include "DataLayout.hpp"
#include "Domain.hpp"
#include "Endpoint.hpp"
#include "GrainSlices.hpp"
#include "LocalRegion.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief Interface for post-processing on transfer reception.
     *
     * Allows to abstract away any post-processing that needs to be done after a transfer completes.
     */
    class IngressProtocol

    {
    public:
        virtual ~IngressProtocol() = default;

        /** \brief Process a completion with the given immediate data.
         * \param immData The immediate data from the completion.
         */
        virtual void processCompletion(std::uint32_t immData) = 0;
    };

    /** \brief Interface for transfer operations.
     *
     * Used to abstract away the details of how data is transferred to remote targets.
     */
    class EgressProtocol
    {
    public:
        virtual ~EgressProtocol() = default;

        /** \brief Transfer a grain to a remote target.
         * \param localRegion The local region to transfer from.
         * \param remoteIndex The index of the remote grain to transfer to.
         * \param payloadOffset The payload offset within the grain.
         * \param sliceRange The range of slices to transfer.
         * \param destAddr The destination address. This is ignored for connection-oriented endpoints.
         * \return The number of requests posted to the endpoint work queue.
         */
        virtual std::size_t transferGrain(LocalRegion const& localRegion, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
            SliceRange const& sliceRange, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) = 0;
    };

    /** \brief Select an appropriate ingress protocol based on the data layout
     * \param domain The domain to use.
     * \param layout The data layout.
     * \param regions The regions involved.
     * \return A unique pointer to the selected ingress protocol.
     */
    std::unique_ptr<IngressProtocol> selectProtocol(std::shared_ptr<Domain> domain, DataLayout const& layout, std::vector<Region> const& regions);

    /** \brief Select an appropriate egress protocol based on the data layout
     * \param ep The endpoint to use.
     * \param layout The data layout.
     * \param targetInfo The target information.
     * \return A unique pointer to the selected egress protocol.
     */
    std::unique_ptr<EgressProtocol> selectProtocol(std::shared_ptr<Endpoint> ep, DataLayout const& layout, TargetInfo const& targetInfo);

}
