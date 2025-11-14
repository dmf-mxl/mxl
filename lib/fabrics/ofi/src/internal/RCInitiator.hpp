// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <variant>
#include <vector>
#include "Completion.hpp"
#include "Domain.hpp"
#include "Endpoint.hpp"
#include "Event.hpp"
#include "Initiator.hpp"

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
        RCInitiatorEndpoint(Endpoint, FabricAddress, std::vector<RemoteRegion>);

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
        void consume(Event);

        /** \brief Consume a completion that was posted to the associated completion queue.
         */
        void consume(Completion);

        /** \brief Post a data transfer request to this endpoint where the user have to supply the remote region grain index and offset directly.
         */
        void postTransferWithRemoteGrainIndex(LocalRegion const& localRegion, std::uint64_t remoteIndex, std::uint64_t remoteOffset,
            std::uint32_t size, std::uint16_t validSlices);

        /** \brief Post a data transfer request to this endpoint using the grain index to select the remote region.
         */
        void postTransferWithGrainIndex(LocalRegion const& localRegion, std::uint64_t grainIndex, std::uint64_t remoteOffset, std::uint32_t size,
            std::uint16_t validSlices);

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
            std::size_t pending; /// The number of currently pending write requests.
        };

        /** \brief The shutdown state.
         *
         * The endpoint is shutting down and is waiting for a Event::Shutdown event.
         */
        struct Shutdown
        {
            Endpoint ep;
        };

        /** \brief The endpoint is done and can be evicted from the initiator.
         */
        struct Done
        {};

        /** \brief The various states that the endpoint can be in are stored inside a variant that we move from and then back into when processing
         * events.
         */
        using State = std::variant<Idle, Connecting, Connected, Shutdown, Done>;

    private:
        /** \brief Handle a completion error event.
         */
        void handleCompletionError(Completion::Error);

        /** \brief Handle a completion data event.
         */
        void handleCompletionData(Completion::Data);

        /** \brief Restarts the endpoint. Produces a new Idle state from any previous state.
         */
        Idle restart(Endpoint const&);

    private:
        State _state;                       /**< The internal state object. */
        FabricAddress _addr;                /**< The remote fabric address to connect to. */
        std::vector<RemoteRegion> _regions; /**< Descriptions of the remote memory regions where we need to write our grains. */
    };

    /** \brief An initiator that uses reliable connected endpoints to transfer data to targets.
     */
    class RCInitiator : public Initiator
    {
    public:
        /** \brief Set up a fresh initiator without any targets assigned to it.
         *
         * The setup phase includes selecting a provider, register memory regions for all local media buffers and create all libfabric components such
         * as completion and event queues.
         *
         * \param config The configuration to use for setting up the target.
         * \return A newly setup RCInitiator object.
         */
        static std::unique_ptr<RCInitiator> setup(mxlInitiatorConfig const& config);

        /** \copydoc Initiator::addTarget(TargetInfo const&)
         */
        void addTarget(TargetInfo const& targetInfo) final;

        /** \copydoc Initiator::removeTarget(TargetInfo const&)
         */
        void removeTarget(TargetInfo const& targetInfo) final;

        /** \copydoc Initiator::transferGrain(std::uint64_t, std::uint64_t, std::uint32_t, std::uint16_t)
         */
        void transferGrain(std::uint64_t grainIndex, std::uint64_t offset, std::uint32_t size, std::uint16_t validSlices) final;

        /** \copydoc Initiator::transferGrainToTarget(Endpoint::Id, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint32_t,
         * std::uint16_t)
         */
        void transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t localOffset, std::uint64_t remoteIndex,
            std::uint64_t remoteOffset, std::uint32_t size, std::uint16_t validSlices) final;

        /** \copydoc Initiator::makeProgress()
         */
        bool makeProgress() final;

        /** \copydoc Initiator::makeProgressBlocking()
         */
        bool makeProgressBlocking(std::chrono::steady_clock::duration) final;

    private:
        /** \brief Returns true if any of the endpoints contained in this initiator have pending work.
         */
        [[nodiscard]]
        bool hasPendingWork() const noexcept;

        /** \brief When making progress and using blocking queue reads, this is the minimum interval at which the event queue will be read.
         */
        constexpr static auto EQPollInterval = std::chrono::milliseconds(100);

        /*\brief Construct the RCInitiator.
         *
         * \param domain The domain to create the initiator on.
         * \param cq The completion queue to use for all endpoints created by this initiator.
         * \param eq The event queue to use for all endpoints created by this initiator.
         */
        RCInitiator(std::shared_ptr<Domain>, std::shared_ptr<CompletionQueue>, std::shared_ptr<EventQueue>);

        /** \brief Block on the completion queue with a timeout.
         */
        void blockOnCQ(std::chrono::steady_clock::duration);

        /** \brief Poll the completion queue and process the events until the queue is empty.
         */
        void pollCQ();

        /** \brief Poll the event queue and process the returned events until the queue is empty.
         */
        void pollEQ();

        /** \brief Try to activate any idle endpoints.
         */
        void activateIdleEndpoints();

        /** \brief Evict any dead endpoints that are no longer used.
         */
        void evictDeadEndpoints();

    private:
        std::shared_ptr<Domain> _domain;
        std::shared_ptr<CompletionQueue> _cq;                   /**< Completion Queue shared by all endpoints. */
        std::shared_ptr<EventQueue> _eq;                        /**< Event Queue shared by all endpoints. */

        std::vector<LocalRegion> _localRegions;                 /**< Local registered memory regions used for data transfers. */

        std::map<Endpoint::Id, RCInitiatorEndpoint> _targets{}; /**< Targets managed by this initiator. */
    };
}
