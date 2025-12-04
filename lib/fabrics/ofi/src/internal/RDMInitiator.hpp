// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <rdma/fabric.h>
#include "mxl/fabrics.h"
#include "Endpoint.hpp"
#include "GrainSlices.hpp"
#include "Initiator.hpp"
#include "Protocol.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief A single endpoint within an RDMInitiator.
     */
    class RDMInitiatorTarget
    {
    public:
        /** \brief Construct a new RDMInitiatorEndpoint object.
         *
         * \param ep The endpoint to use for posting transfers.
         * \param addr The fabric address of the remote endpoint.
         * \param regions The remote memory regions to transfer data to.
         */
        RDMInitiatorTarget(std::unique_ptr<EgressProtocol> proto, TargetInfo remoteInfo);

        /** \brief Returns true if the endpoint is idle and could be actived.
         */
        [[nodiscard]]
        bool isIdle() const noexcept;

        /** \brief Try to activate the endpoint.
         *
         * The endpoint is activated by adding the remote address to the address vector.
         */
        void activate(Endpoint& ep);

        /** \brief Initiate a shutdown process.
         *
         * The endpoint will have pending work until a shutdown or error event will be received. After which it can be evicted from the initiator.
         */
        void shutdown(Endpoint& ep);

        /** \brief Post a data transfer request to this endpoint.
         */
        void transfer(Endpoint& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t remotePayloadOffset,
            SliceRange const& sliceRange);

        /** \brief Returns true if there's pending work for this target.
         */
        [[nodiscard]]
        bool hasPendingWork() const noexcept;

        /** \brief Handle a completion event for this endpoint.
         */
        void handleCompletion(Endpoint& ep, Completion completion);

    private:
        /** \brief The idle state.
         *
         * In this state the endpoint waits to be activated.
         */
        struct Idle
        {};

        /** \brief The endpoint activated state.
         *
         * In this state, the remote endpoint was added to the address vector, meaning we can write to the remote endpoint.
         */
        struct Activated
        {
            ::fi_addr_t fiAddr; /**< Address index in address vector. */
        };

        /** \brief The endpoint is done and can be evicted from the initiator.
         */
        struct Done
        {};

        /** \brief The various states that the endpoint can be in are stored inside a variant that we move from and then back into when processing
         * events.
         */
        using State = std::variant<Idle, Activated, Done>;

    private:
        State _state; /**< The current state of the endpoint. */
        std::unique_ptr<EgressProtocol> _proto;

        TargetInfo _remoteInfo;
    };

    /** \brief An initiator that uses reliable datagram (RDM) endpoints for data transfers.
     *
     * RDM endpoints provide connectionless communication with reliability guarantees. An address vector is used to manage destination addresses.
     */
    class RDMInitiator : public Initiator
    {
    public:
        static std::unique_ptr<RDMInitiator> setup(mxlFabricsInitiatorConfig const&);

        /** \copydoc Initiator::addTarget()
         */
        void addTarget(TargetInfo const&) override;

        /** \copydoc Initiator::removeTarget()
         */
        void removeTarget(TargetInfo const&) override;

        void shutdown() override;

        /** \copydoc Initiator::transferGrain()
         */
        void transferGrain(std::uint64_t grainIndex, std::uint16_t startSlice, std::uint16_t endSlice) override;

        /** \copydoc Initiator::transferGrainToTarget()
         */
        void transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t payloadOffset,
            std::uint16_t startSlice, std::uint16_t endSlice) override;

        /** \copydoc Initiator::makeProgress()
         */
        bool makeProgress() override;

        /** \copydoc Initiator::makeProgressBlocking()
         */
        bool makeProgressBlocking(std::chrono::steady_clock::duration) override;

    private:
        /** \brief Construct a new RDMInitiator object.
         *
         * \param ep The local endpoint to use for all transfers.
         */
        RDMInitiator(Endpoint, std::unique_ptr<EgressProtocolTemplate>);

        /** \brief Find a remote target by its endpoint id.
         *
         * \throws Exception::notFound if the Endpoint::Id does not map to any known remote endpoints.
         */
        RDMInitiatorTarget& findRemoteByEndpoint(Endpoint::Id id);

        /** \brief Find a remote target by its completion token.
         *
         * \throws Exception::notFound if the Completion::Token does not map to any known target.
         */
        RDMInitiatorTarget& findRemoteByToken(Completion::Token token);

        /** \brief Returns true if any of the endpoints contained in this initiator have pending work.
         */
        [[nodiscard]]
        bool hasPendingWork() const noexcept;

        /** \brief Block on the completion queue with a timeout.
         */
        void blockOnCQ(std::chrono::steady_clock::duration);

        /** \brief Poll the completion queue and process the events until the queue is empty.
         */
        void pollCQ();

        /** \brief Try to activate any idle endpoints.
         */
        void activateIdleEndpoints();

        /** \brief Consume a completion entry
         */
        void processCompletion(Completion);

        /** \brief Handle a completion error event.
         */
        void handleCompletionError(Completion::Error);

        /** \brief Handle a completion data event.
         */
        void handleCompletionData(Completion::Data);

    private:
        Endpoint _endpoint; /** Shared endpoint used for all transfers. */

        std::unique_ptr<EgressProtocolTemplate> _proto;

        /** Targets managed by this initiator. Each target has its own remote address and remote memory regions. */
        std::map<Completion::Token, RDMInitiatorTarget> _targets{};
        std::map<Endpoint::Id, Completion::Token> _remoteEndpoints{};
    };
}
