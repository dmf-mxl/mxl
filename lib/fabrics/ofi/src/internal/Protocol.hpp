#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <rdma/fabric.h>
#include "DataLayout.hpp"
#include "Endpoint.hpp"
#include "GrainSlices.hpp"
#include "Region.hpp"
#include "Target.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief Interface for post-processing on transfer reception.
     *
     * Allows to abstract away any post-processing that needs to be done after a transfer completes. */
    class IngressProtocol
    {
    public:
        virtual ~IngressProtocol() = default;

        /** \brief Register local memory regions used in this protocol.
         * \param domain The domain to register memory with.
         * \return A vector of RemoteRegion representing the registered memory regions for remote access.
         */
        virtual std::vector<RemoteRegion> registerMemory(std::shared_ptr<Domain> domain) = 0;

        /** \brief Start receiving.
         */
        virtual void start(Endpoint&) = 0;

        /** \brief Process a completion with the given immediate data.
         * \param immData The immediate data from the completion.
         */
        virtual Target::ReadResult processCompletion(Endpoint&, Completion const&) = 0;

        /** \brief Destroy the protocol object.
         */
        virtual void destroy() = 0;
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
        virtual void transferGrain(Endpoint& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
            SliceRange const& sliceRange, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) = 0;

        /** \brief Process a completion event. Any post-processing after a transfer should be done here.
         */
        virtual void processCompletion(Completion::Data const&) = 0;

        /** \brief Check if there is uncompleted requests.
         */
        [[nodiscard]]
        virtual bool hasPendingWork() const = 0;

        /** \brief Destroy the protocol object.
         * \return The number of pending transfers.
         */
        virtual std::size_t destroy() = 0;
    };

    class EgressProtocolTemplate
    {
    public:
        virtual ~EgressProtocolTemplate() = default;

        virtual void registerMemory(std::shared_ptr<Domain> domain) = 0;

        virtual std::unique_ptr<EgressProtocol> createInstance(Completion::Token token, TargetInfo remoteInfo) = 0;
    };

    /** \brief Select an appropriate ingress protocol based on the data layout
     * \param layout The data layout.
     * \param regions The regions involved.
     * \return A unique pointer to the selected ingress protocol.
     */
    std::unique_ptr<IngressProtocol> selectIngressProtocol(DataLayout const& layout, std::vector<Region> regions);

    /** \brief Select an appropriate egress protocol based on the data layout
     * \param layout The data layout.
     * \param regions The regions involved.
     * \return A unique pointer to the selected egress protocol.
     */
    std::unique_ptr<EgressProtocolTemplate> selectEgressProtocol(DataLayout const& layout, std::vector<Region> regions);
}
