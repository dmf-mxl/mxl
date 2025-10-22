// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

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

    /// A single endpoint connected to a target that will be created when the user adds a target to an initiator.
    class RCInitiatorEndpoint
    {
    public:
        RCInitiatorEndpoint(Endpoint, FabricAddress, std::vector<RemoteRegion>);

        /// Returns true if there is any pending events that the endpoint is waiting for, and for which
        /// the queues must be polled
        [[nodiscard]]
        bool hasPendingWork() const noexcept;

        /// Returns true if the endpoint is idle and could be actived.
        [[nodiscard]]
        bool isIdle() const noexcept;

        /// Returns true if the endpoint was shut down and can be evicted from the initiator.
        [[nodiscard]]
        bool canEvict() const noexcept;

        /// Initiate a shutdown process. The endpoint will have pending work until a shutdown or error event will be
        /// received. After which it can be evicted from the initiator.
        void shutdown();

        /// Try to activate the endpoint. The endpoint has an internal timer to slow down repeated failures to activate and/or connect.
        /// Until this timer has elapsed, the activation function will do nothing, and RCInitiatorEndpoint::isIdle() will return true.
        /// When the endpoint is eventually actived, it will have pending work until it is connected.
        void activate(std::shared_ptr<CompletionQueue> const& cq, std::shared_ptr<EventQueue> const& eq);

        /// Consume an event that was posted to the associated event queue.
        void consume(Event);

        /// Consume a completion that was posted to the associated completion queue.
        void consume(Completion);

        /// Post a data transfer request to this endpoint.
        void postTransfer(LocalRegion const& localRegion, std::uint64_t index);

    private:
        /// The idle state. In this state the endpoint waits to be activated. This will happen immediately if the
        /// endpoint was newly created, and after about 5 seconds if the endpoint has gone into the idle state because of a problem.
        struct Idle
        {
            Endpoint ep;
            std::chrono::steady_clock::time_point idleSince; /// Time since the endpoint has been idle.
        };

        /// The connecting state. The endpoint has initiated a connetion to a target and is waiting for
        /// a Event::Connected event to be posted to its associated event queue.
        struct Connecting
        {
            Endpoint ep;
        };

        /// The connected state. The endpoint is connected to a target and can receive write requests.
        struct Connected
        {
            Endpoint ep;
            std::size_t pending; /// The number of currently pending write requests.
        };

        /// The shutdown state. The endpoint is shutting down and is waiting for a Event::Shutdown event.
        struct Shutdown
        {
            Endpoint ep;
        };

        /// The endpoint is done and can be evicted from the initiator.
        struct Done
        {};

        /// The various states that the endpoint can be in are stored inside a variant that we move
        /// from and then back into when processing events.
        using State = std::variant<Idle, Connecting, Connected, Shutdown, Done>;

    private:
        void handleCompletionError(Completion::Error); /// Handles a completion error event.
        void handleCompletionData(Completion::Data);   /// Handle a completion data event.

        /// Restarts the endpoint. Produces a new Idle state from any previous state.
        Idle restart(Endpoint const&);

    private:
        State _state;                       /// The internal state object
        FabricAddress _addr;                /// The remote fabric address to connect to.
        std::vector<RemoteRegion> _regions; /// Descriptions of the remote memory regions where we need to write our grains.
    };

    class RCInitiator : public Initiator
    {
    public:
        ~RCInitiator() override = default;

        /// Set up a fresh initiator without any targets assigned to it
        static std::unique_ptr<RCInitiator> setup(mxlInitiatorConfig const& config);

        /// Add a target to the initiator. The is a non-blocking operation. The local endpoint for this target will only actively start connecting
        /// to its remote counterpart when control is passed to makeProgress() or makeProgressBlocking().
        /// The local endpoint for this target will only accept write requests after the progress functions return false.
        void addTarget(TargetInfo const& targetInfo) final;

        /// Remove a target from the initiator. This is a non-blocking operation. The target is only removed after
        /// makeProgress() or makeProgressBlocking() returns false.
        void removeTarget(TargetInfo const& targetInfo) final;

        /// Transfer a grain to all targets. This is a non-blocking operation.
        /// The transfer is complete only after makeProgress() or makeProgressBlocking() returns false.
        void transferGrain(uint64_t grainIndex) final;

        /// The actual work of connecting, shutting down and transferring grains is done when this function is called.
        /// This is the non blocking version. That means it will do all currently pending work, check all queues, but not wait
        /// until events have arrived. The function returns false when all remaining work has been completed.
        bool makeProgress() final;

        /// The actual work of connecting, shutting down and transferring grains is done while control is passed to this function.
        /// This is the blocking version of this function. This means that the function will only return if all pending work has been completed,
        /// or the timeout has been reached. The function returns false when all remaining work has been completed.
        bool makeProgressBlocking(std::chrono::steady_clock::duration) final;

    private:
        /// Returns true if any of the endpoints contained in this initiator have pending work.
        [[nodiscard]]
        bool hasPendingWork() const noexcept;

        /// When makeing progress using blocking queue reads, this is the minimum interval at which the event queue will be read.
        constexpr static auto EQPollInterval = std::chrono::milliseconds(100);

        RCInitiator(std::shared_ptr<Domain>, std::shared_ptr<CompletionQueue>, std::shared_ptr<EventQueue>);

        /// Block on the completion queue with a timeout.
        void blockOnCQ(std::chrono::steady_clock::duration);

        /// Poll the completion queue and process the events until the queue is empty.
        void pollCQ();

        /// Poll the event queue and process the returned events until the queue is empty.
        void pollEQ();

        /// Try to activate any idle endpoints.
        void activateIdleEndpoints();

        /// Evict any dead endpoints that are no longer used.
        void evictDeadEndpoints();

    private:
        std::shared_ptr<Domain> _domain;
        std::shared_ptr<CompletionQueue> _cq;
        std::shared_ptr<EventQueue> _eq;

        std::vector<LocalRegion> _localRegions;

        // Default initialized
        std::map<Endpoint::Id, RCInitiatorEndpoint> _targets{};
    };
}
