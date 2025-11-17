// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include "mxl/fabrics.h"
#include "Endpoint.hpp"
#include "Protocol.hpp"
#include "QueueHelpers.hpp"
#include "Target.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief Reliable Datagram (RDM) Target implementation.
     */
    class RDMTarget : public Target
    {
    public:
        /** \brief Set up a fresh RDMTarget and its associated TargetInfo based on the given configuration.
         *
         * \param config The configuration to use for setting up the target.
         * \return A pair consisting of the newly setup RDMTarget and its associated TargetInfo.
         */

        static std::pair<std::unique_ptr<RDMTarget>, std::unique_ptr<TargetInfo>> setup(mxlTargetConfig const&);

        /** \copydoc Target::read()
         */
        Target::ReadResult read() override;

        /** \copydoc Target::readBlocking()
         */
        Target::ReadResult readBlocking(std::chrono::steady_clock::duration timeout) override;

    private:
        /** \brief Construct an RDMTarget with the given endpoint and immediate data location.
         *
         * \param endpoint The endpoint to use for communication.
         * \param immData The immediate data location to use for transfers.
         */
        RDMTarget(Endpoint endpoint, std::unique_ptr<IngressProtocol> proto, std::unique_ptr<ImmediateDataLocation> immData);

        /** \brief Internal method to drive progress based on the current state.
         *
         * \param timeout The maximum duration to block waiting for progress.
         * \return The result of the read operation.
         */
        template<QueueReadMode>
        Target::ReadResult makeProgress(std::chrono::steady_clock::duration timeout);

    private:
        Endpoint _endpoint;
        std::unique_ptr<IngressProtocol> _proto;         /**< Protocol used for processing incoming transfers */

        std::unique_ptr<ImmediateDataLocation> _immData; /**< Immediate data for transfers */
    };
}
