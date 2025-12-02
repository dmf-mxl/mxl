#pragma once

#include "Protocol.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief Ingress protocol for RMA writer endpoint.
     *
     * Handles processing of completions when paired with an endpoint that does remote write to our buffers without bounce buffering.
     */
    class RMAGrainIngressProtocol final : public IngressProtocol
    {
    public:
        RMAGrainIngressProtocol(std::vector<Region> regions);

        /** \brief
         */
        std::vector<RemoteRegion> registerMemory(std::shared_ptr<Domain> domain) override;

        /** \brief Start receiving
         */
        void start(Endpoint&) override;

        /** \brief Process a completion with the given immediate data.
         * \param immData The immediate data from the completion.
         */
        Target::ReadResult processCompletion(Endpoint&, Completion const&) override;

        /** \brief Destroy the protocol object.
         * \return The endpoint associated with the protocol and the number of pending transfers.
         */
        void destroy() override;

    private:
        LocalRegion immDataRegion();

        std::vector<Region> _regions;
        std::optional<std::vector<LocalRegion>> _localRegions{};
        std::optional<Target::ImmediateDataLocation> _immDataBuffer{};
    };

}
