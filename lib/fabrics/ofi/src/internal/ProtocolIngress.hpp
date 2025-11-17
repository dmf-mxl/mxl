#pragma once

#include "Protocol.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief Ingress protocol for RMA writer endpoint.
     *
     * Handles processing of completions when paired with an endpoint that does remote write to our buffers without bounce buffering.
     */
    class IngressProtocolWriter final : public IngressProtocol
    {
    public:
        IngressProtocolWriter() = default;

        /** \copydoc Protocol::processCompletion() */
        void processCompletion(std::uint32_t immData) override;
    };

}
