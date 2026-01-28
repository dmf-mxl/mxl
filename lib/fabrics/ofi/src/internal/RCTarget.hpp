// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <variant>
#include "mxl/fabrics.h"
#include "Endpoint.hpp"
#include "PassiveEndpoint.hpp"
#include "Protocol.hpp"
#include "QueueHelpers.hpp"
#include "Target.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief Reliable Connected (RC) Target implementation.
     *
     * An RC Target uses connection-oriented endpoint type. It listens for incoming connection requests from an Initiator to establish connection.
     * Once connected, it polls its completion queues to see if new data can be consumed. be read from the connected endpoint.
     */
    class RCTarget : public Target
    {
    public:
        /** \brief Set up a fresh RCTarget and its associated TargetInfo based on the given configuration.
         *
         * \param config The configuration to use for setting up the target.
         * \return A pair consisting of the newly setup RCTarget and its associated TargetInfo.
         */
        [[nodiscard]]
        static std::pair<std::unique_ptr<RCTarget>, std::unique_ptr<TargetInfo>> setup(mxlFabricsTargetConfig const& config);

        /** \copydoc Target::read()
         */
        Target::ReadResult read() final;

        /** \copydoc Target::readBlocking()
         */
        Target::ReadResult readBlocking(std::chrono::steady_clock::duration timeout) final;

        void shutdown() override;

    private:
        /** \brief The wait for connection request state.
         *
         * The RCTarget has a passive endpoint, and is waiting for an incoming connection request.
         */
        struct WaitForConnectionRequest
        {
            PassiveEndpoint pep;
        };

        /** \brief The wait for connection state.
         *
         * The RCTarget has accepted a connection request, and is waiting for the connected event.
         */
        struct WaitForConnection
        {
            Endpoint ep;
        };

        /** \brief The connected state.
         *
         * The RCTarget is now connected to an Initiator, and can receive data.
         */
        struct Connected
        {
            Endpoint ep;
        };

        /** \brief The internal state of the RCTarget.
         */
        using State = std::variant<WaitForConnectionRequest, WaitForConnection, Connected>;

    private:
        /** \brief Construct a new RCTarget associated with the given domain and passive endpoint.
         *
         * \param domain The domain to create the RCTarget on.
         * \param pep The passive endpoint to use for listening for incoming connection requests.
         */
        RCTarget(PassiveEndpoint pep, std::unique_ptr<IngressProtocol> proto, std::shared_ptr<Domain> domain);

        /** \brief Internal method to drive progress based on the current state.
         *
         * \param timeout The maximum duration to block waiting for progress.
         * \return The result of the read operation.
         */
        template<QueueReadMode>
        Target::ReadResult makeProgress(std::chrono::steady_clock::duration timeout);

        [[nodiscard]]
        static PassiveEndpoint makeListener(std::shared_ptr<Fabric> fabric);

    private:
        std::unique_ptr<IngressProtocol> _proto;
        std::shared_ptr<Domain> _domain;
        State _state; /**< The current state of the RCTarget. */
    };
}
