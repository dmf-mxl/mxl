// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
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
#include "Initiator.hpp"
#include "Protocol.hpp"
#include "SliceRange.hpp"
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

        /** \brief Stop transfers to this target.
         *
         * If writes are still outstanding, the target goes to the removing state. It keeps its address vector entry and its protocol
         * instance until finishRemoval() finds that the writes have completed. Otherwise the address vector entry is removed right away and
         * the target can be evicted.
         */
        void shutdown(Endpoint& ep);

        /** \brief Complete the removal once all outstanding writes have completed or RemoveTimeout has elapsed.
         *
         * Removes the address vector entry and moves the target to the done state. In any other state than the removing state, this is a
         * no-op.
         */
        void finishRemoval(Endpoint& ep);

        /** \brief Returns true if the target was shut down and can be evicted from the initiator.
         */
        [[nodiscard]]
        bool canEvict() const noexcept;

        /** \brief Upper bound for the removing state. A target that disappears may never complete the outstanding writes.
         */
        constexpr static auto RemoveTimeout = std::chrono::seconds(2);

        /** \brief Post a grain data transfer request to this endpoint.
         */
        void transferGrain(Endpoint const& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t remotePayloadOffset,
            SliceRange const& sliceRange);

        /** \brief Post a sample data transfer request to this endpoint.
         */
        void transferSamples(Endpoint const& ep, std::uint64_t headIndex, std::size_t count);

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

        /** \brief The removing state.
         *
         * The target was removed while writes to it were outstanding. No new writes are posted. The address vector entry stays valid until
         * the outstanding writes have completed.
         */
        struct Removing
        {
            ::fi_addr_t fiAddr;                             /**< Address index in address vector. */
            std::chrono::steady_clock::time_point deadline; /**< The removal is completed after this point in time even with writes outstanding. */
        };

        /** \brief The endpoint is done and can be evicted from the initiator.
         */
        struct Done
        {};

        /** \brief The various states that the endpoint can be in are stored inside a variant that we move from and then back into when processing
         * events.
         */
        using State = std::variant<Idle, Activated, Removing, Done>;

    private:
        State _state; /**< The current state of the endpoint. */
        std::unique_ptr<EgressProtocol> _proto;

        TargetInfo _remoteInfo;
    };

    /** \brief An initiator that uses reliable datagram (RDM) endpoints for data transfers.
     *
     * RDM endpoints provide connectionless communication with reliability guarantees. An address vector is used to manage destination addresses.
     */
    class RDMInitiator /*final*/ : public Initiator
    {
    public:
        /** \brief Set up a new RDMInitiator.
         *
         * \param config The configuration to use for setting up the initiator.
         * \param info Fabric info already resolved from the interface config.
         * \param options Optional tuning parameters (e.g. completion queue depth).
         */
        [[nodiscard]]
        static std::unique_ptr<RDMInitiator> setup(mxlFabricsInitiatorConfig const& config, FabricInfoView info,
            InitiatorSetupOptions const& options = {});

        /** \copydoc Initiator::addTarget()
         */
        virtual void addTarget(TargetInfo const& remoteTargetInfo) final;

        /** \copydoc Initiator::removeTarget()
         */
        virtual void removeTarget(TargetInfo const& remoteTargetInfo) final;

        /** \copydoc Initiator::shutdown()
         *
         * Removes all targets and makes progress until the outstanding writes to each one have completed.
         */
        virtual void shutdown() final;

        /** \copydoc Initiator::transferGrain()
         */
        virtual void transferGrain(std::uint64_t grainIndex, std::uint16_t startSlice, std::uint16_t endSlice) final;

        /** \copydoc Initiator::transferGrainToTarget()
         */
        virtual void transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t payloadOffset,
            std::uint16_t startSlice, std::uint16_t endSlice) final;

        /** \copydoc Initiator::transferSamples()
         */
        virtual void transferSamples(std::uint64_t headIndex, std::size_t count) final;

        /** \copydoc Initiator::makeProgress()
         */
        virtual Initiator::MakeProgressResult makeProgress() final;

        /** \copydoc Initiator::makeProgressBlocking()
         */
        virtual Initiator::MakeProgressResult makeProgressBlocking(std::chrono::steady_clock::duration timeout) final;

    private:
        /** \brief Construct a new RDMInitiator object.
         *
         * \param ep The local endpoint to use for all transfers.
         */
        RDMInitiator(Endpoint ep, std::unique_ptr<EgressProtocolTemplate> proto);

        /** \brief Find a remote target by its endpoint id.
         *
         * \throws Exception::notFound if the Endpoint::Id does not map to any known remote endpoints.
         */
        [[nodiscard]]
        RDMInitiatorTarget& findRemoteByEndpoint(Endpoint::Id id);

        /** \brief Find a remote target by its completion token.
         *
         * \throws Exception::notFound if the Completion::Token does not map to any known target.
         */
        [[nodiscard]]
        RDMInitiatorTarget& findRemoteByToken(Completion::Token token);

        /** \brief Returns true if any of the endpoints contained in this initiator have pending work.
         */
        [[nodiscard]]
        Initiator::MakeProgressResult afterProgressResult() const noexcept;

        /** \brief Block on the completion queue until a completion arrives or the timeout elapses, then process all available completions.
         */
        void blockOnCQ(std::chrono::steady_clock::duration timeout);

        /** \brief Poll the completion queue and process the events until the queue is empty.
         */
        void pollCQ();

        /** \brief Try to activate any idle endpoints.
         */
        void activateIdleEndpoints();

        /** \brief Complete the removal of targets whose outstanding writes have completed, and evict them.
         */
        void evictRemovedTargets();

        /** \brief Consume a completion entry
         */
        void processCompletion(Completion completion);

        /** \brief Handle a completion error event.
         */
        void handleCompletionError(Completion::Error error);

        /** \brief Handle a completion data event.
         */
        void handleCompletionData(Completion::Data data);

        /** \brief Upper bound for shutdown(). It is longer than RDMInitiatorTarget::RemoveTimeout, so that each removal ends on its own.
         */
        constexpr static auto ShutdownTimeout = RDMInitiatorTarget::RemoveTimeout + std::chrono::seconds(1);

        /** \brief Sleep time between two queue reads in shutdown().
         */
        constexpr static auto ShutdownPollInterval = std::chrono::milliseconds(1);

    private:
        std::unique_ptr<EgressProtocolTemplate> _proto;

        /** Targets managed by this initiator. Each target has its own remote address and remote memory regions. */
        std::map<Completion::Token, RDMInitiatorTarget> _targets{};
        std::map<Endpoint::Id, Completion::Token> _remoteEndpoints{};

        // The endpoint is declared last so that it is destroyed before the protocols. Writes can read from memory that the protocols own
        // until the endpoint is closed.
        Endpoint _endpoint; /** Shared endpoint used for all transfers. */
    };
}
