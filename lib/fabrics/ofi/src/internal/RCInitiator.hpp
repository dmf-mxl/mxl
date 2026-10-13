// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <variant>
#include "Domain.hpp"
#include "Endpoint.hpp"
#include "Event.hpp"
#include "Initiator.hpp"
#include "Protocol.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief A single endpoint connected to a target that will be created when the user adds a target to an initiator.
     */
    class RCInitiatorEndpoint
    {
    public:
        /** \brief Construct a new RCInitiatorEndpoint object.
         *
         * \param Endpoint The underlying endpoint object.
         * \param FabricAddress The fabric address of the remote target.
         * \param RemoteRegions The remote memory regions on the target where data must be written to.
         */
        RCInitiatorEndpoint(Endpoint, std::unique_ptr<EgressProtocol> proto, TargetInfo info);

        /** \brief Get the target info of the remote endpoint.
         */
        [[nodiscard]]
        TargetInfo const& info() const noexcept;

        /** \brief Returns true if there is any pending events that the endpoint is waiting for, and for which
         * the queues must be polled.
         */
        [[nodiscard]]
        bool hasPendingWork() const noexcept;

        /** \brief Returns true if the endpoint is idle and could be actived.
         */
        [[nodiscard]]
        bool isIdle() const noexcept;

        /** \brief Returns true if the endpoint was shut down and can be evicted from the initiator.
         */
        [[nodiscard]]
        bool canEvict() const noexcept;

        /** \brief Move the endpoint out of the flushing state when the flush is complete.
         *
         * The flush is complete when a shutdown or error event was received and all operations that were outstanding at shutdown have
         * completed, or when FlushTimeout has elapsed since the flush started. The endpoint then goes to the idle state if it is to
         * reconnect, and to the done state otherwise. In any other state, this is a no-op.
         */
        void finishFlush();

        /** \brief Initiate a shutdown process.
         *
         * The endpoint will have pending work until a shutdown or error event will be received. After which it can be evicted from the initiator.
         */
        void shutdown();

        /** \brief Try to activate the endpoint.
         *
         * The endpoint has an internal timer to slow down repeated failures to activate and/or connect.
         * Until this timer has elapsed, the activation function will do nothing, and RCInitiatorEndpoint::isIdle() will return true.
         * When the endpoint is eventually actived, it will have pending work until it is connected.
         */
        void activate(std::shared_ptr<CompletionQueue> const& cq, std::shared_ptr<EventQueue> const& eq);

        /** \brief Consume an event that was posted to the associated event queue.
         */
        void consume(Event event);

        /** \brief Consume a completion that was posted to the associated completion queue.
         */
        void consume(Completion completion);

        /** \brief Post a data transfer request to this endpoint.
         */
        void transferGrain(std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint64_t remotePayloadOffset, SliceRange const& sliceRange);

        /** \brief Post a data transfer request to this endpoint.
         */
        void transferSamples(std::uint64_t headIndex, std::size_t count);

        /** \brief Upper bound for the flushing state. Some providers do not report a local shutdown event, and a peer that disappears
         * may never complete the outstanding operations.
         */
        constexpr static auto FlushTimeout = std::chrono::seconds(2);

    private:
        /** \brief The idle state.
         *
         * In this state the endpoint waits to be activated. This will happen immediately if the endpoint was newly created, and after about 5 seconds
         * if the endpoint has gone into the idle state because of a problem.
         */
        struct Idle
        {
            Endpoint ep;
            std::chrono::steady_clock::time_point idleSince; /**< Time since the endpoint has been idle. */
        };

        /** \brief The connecting state.
         *
         * The endpoint has initiated a connetion to a target and is waiting for a Event::Connected event to be posted to its associated event queue.
         */
        struct Connecting
        {
            Endpoint ep;
        };

        /** \brief The connected state.
         *
         * The endpoint is connected to a target and can receive write requests.
         */
        struct Connected
        {
            Endpoint ep;
        };

        /** \brief The flushing state.
         *
         * The endpoint is shutting down. It waits for an Event::Shutdown or error event and for a completion of every operation that was
         * outstanding when the shutdown started. Operations that were cancelled by the shutdown complete with an error.
         */
        struct Flushing
        {
            Endpoint ep;
            std::size_t pending;                            /**< Number of outstanding operations that have not completed yet. */
            bool shutdownReceived;                          /**< True once a shutdown or error event was received. */
            bool reconnect;                                 /**< Go to the idle state instead of the done state after the flush. */
            std::chrono::steady_clock::time_point deadline; /**< The flush is abandoned after this point in time. */
        };

        /** \brief The endpoint is done and can be evicted from the initiator.
         */
        struct Done
        {};

        /** \brief The various states that the endpoint can be in are stored inside a variant that we move from and then back into when processing
         * events.
         */
        using State = std::variant<Idle, Connecting, Connected, Flushing, Done>;

    private:
        /** \brief Create a flushing state that waits for the given number of outstanding operations.
         */
        [[nodiscard]]
        static Flushing startFlush(Endpoint ep, std::size_t pending, bool shutdownReceived, bool reconnect);

        /** \brief Restarts the endpoint. Produces a new Idle state from any previous state.
         *
         * The new endpoint has the same id as the old one, so completions of the old endpoint must not be in the completion queue anymore.
         */
        [[nodiscard]]
        Idle restart(Endpoint const& endpoint);

    private:
        // The protocol is declared first so that it is destroyed after the endpoint in _state. Writes can read from memory that the
        // protocol owns until the endpoint is closed.
        std::unique_ptr<EgressProtocol> _proto;
        TargetInfo _info; /**< The target info of the remote endpoint */
        State _state;     /**< The internal state object. */
    };

    /** \brief An initiator that uses reliable connected endpoints to transfer data to targets.
     */
    class RCInitiator /*final*/ : public Initiator
    {
    public:
        /** \brief Set up a fresh initiator without any targets assigned to it.
         *
         * The setup phase includes selecting a provider, register memory regions for all local media buffers and create all libfabric components such
         * as completion and event queues.
         *
         * \param config The configuration to use for setting up the target.
         * \param info Fabric info already resolved from the interface config.
         * \param options Optional tuning parameters (e.g. completion queue depth).
         * \return A newly setup RCInitiator object.
         */
        [[nodiscard]]
        static std::unique_ptr<RCInitiator> setup(mxlFabricsInitiatorConfig const& config, FabricInfoView info,
            InitiatorSetupOptions const& options = {});

        /** \copydoc Initiator::addTarget()
         */
        virtual void addTarget(TargetInfo const& targetInfo) final;

        /** \copydoc Initiator::removeTarget()
         */
        virtual void removeTarget(TargetInfo const& targetInfo) final;

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
        virtual Initiator::MakeProgressResult makeProgressBlocking(std::chrono::steady_clock::duration) final;

        /** \copydoc Initiator::shutdown()
         *
         * Shuts down all endpoints and makes progress until each one has finished flushing.
         */
        virtual void shutdown() final;

    private:
        /** \brief Returns true if any of the endpoints contained in this initiator have pending work.
         */
        [[nodiscard]]
        Initiator::MakeProgressResult afterProgressResult() const noexcept;

        /** \brief Returns true if the initiator has at least 1 target added no matter what the state is.
         */
        [[nodiscard]]
        bool hasTarget() const noexcept;

        /** \brief When making progress and using blocking queue reads, this is the minimum interval at which the event queue will be read.
         */
        constexpr static auto EQPollInterval = std::chrono::milliseconds(100);

        /** \brief Upper bound for shutdown(). It is longer than RCInitiatorEndpoint::FlushTimeout, so that each flush ends on its own.
         */
        constexpr static auto ShutdownTimeout = RCInitiatorEndpoint::FlushTimeout + std::chrono::seconds(1);

        /** \brief Sleep time between two queue reads in shutdown().
         */
        constexpr static auto ShutdownPollInterval = std::chrono::milliseconds(1);

        /*\brief Construct the RCInitiator.
         *
         * \param domain The domain to create the initiator on.
         * \param cq The completion queue to use for all endpoints created by this initiator.
         * \param eq The event queue to use for all endpoints created by this initiator.
         */
        RCInitiator(std::shared_ptr<Domain> domain, std::shared_ptr<CompletionQueue> cq, std::shared_ptr<EventQueue> eq,
            std::unique_ptr<EgressProtocolTemplate> proto);

        /** \brief Block on the completion queue with a timeout.
         */
        [[nodiscard]]
        Initiator::MakeProgressResult blockOnCQ(std::chrono::steady_clock::duration timeout);

        /** \brief Poll the completion queue and process the events until the queue is empty.
         */
        void pollCQ();

        /** \brief Poll the event queue and process the returned events until the queue is empty.
         */
        void pollEQ();

        /** \brief Try to activate any idle endpoints.
         */
        void activateIdleEndpoints();

        /** \brief Move endpoints that have finished flushing to the done state.
         */
        void finishFlushingEndpoints();

        /** \brief Evict any dead endpoints that are no longer used.
         */
        void evictDeadEndpoints();

    private:
        std::shared_ptr<Domain> _domain;
        std::shared_ptr<CompletionQueue> _cq; /**< Completion Queue shared by all endpoints. */
        std::shared_ptr<EventQueue> _eq;      /**< Event Queue shared by all endpoints. */
        std::unique_ptr<EgressProtocolTemplate> _proto;

        std::map<Completion::Token, RCInitiatorEndpoint> _targets{}; /**< Targets managed by this initiator. */
    };
}
