// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

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

        /** \copydoc IngressProtocol::registerMemory()
         */
        std::vector<RemoteRegion> registerMemory(std::shared_ptr<Domain> domain) override;

        /** \copydoc IngressProtocol::start()
         */
        void start(Endpoint&) override;

        /** \copydoc IngressProtocol::processCompletion()
         */
        Target::ReadResult processCompletion(Endpoint&, Completion const&) override;

        /** \copydoc IngressProtocol::destroy()
         */
        void reset() override;

    private:
        LocalRegion immDataRegion();

        std::vector<Region> _regions;
        bool _isMemoryRegistered{false};
        std::optional<Target::ImmediateDataLocation> _immDataBuffer{};
    };

}
