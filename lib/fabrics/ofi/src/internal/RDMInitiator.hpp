// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <rdma/fabric.h>
#include "mxl/fabrics.h"
#include "Address.hpp"
#include "Endpoint.hpp"
#include "Initiator.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{

    class RDMInitiatorEndpoint
    {
    public:
        RDMInitiatorEndpoint(std::shared_ptr<Endpoint> ep, FabricAddress, std::vector<RemoteRegionGroup>);

        /// Returns true if the endpoint is idle and could be actived.
        [[nodiscard]]
        bool isIdle() const noexcept;

        /// Returns true if the endpoint was shut down and can be evicted from the initiator.
        [[nodiscard]]
        bool canEvict() const noexcept;

        /// Try to activate the endpoint.
        void activate();

        /// Initiate a shutdown process. The endpoint will have pending work until a shutdown or error event will be
        /// received. After which it can be evicted from the initiator.
        void shutdown();

        /// Post a data transfer request to this endpoint.
        void postTransfer(LocalRegionGroup const& localRegion, uint64_t index);

    private:
        /// The idle state. In this state the endpoint waits to be activated.
        struct Idle
        {};

        /// Remote endpoint was added to the address vector, in this state we can write to the remote endpoint.
        struct Added
        {
            ::fi_addr_t fiAddr; // Address index in address vector
        };

        /// The endpoint is done and can be evicted from the initiator.
        struct Done
        {};

        using State = std::variant<Idle, Added, Done>;

        State _state;
        std::shared_ptr<Endpoint> _ep; // The endpoint used to post transfer with. There is only one endpoint shared for all targets in constrast to
                                       // the RCInitiator where each target will have their own endppint.
        FabricAddress _addr;           // The remote fabric address to transfer payloads to
        std::vector<RemoteRegionGroup> _regions; /// Descriptions of the remote memory regions where we need to write our grains.
    };

    class RDMInitiator : public Initiator
    {
    public:
        ~RDMInitiator() override = default;

        static std::unique_ptr<RDMInitiator> setup(mxlInitiatorConfig const&);

        void addTarget(TargetInfo const&) final;
        void removeTarget(TargetInfo const&) final;
        void transferGrain(uint64_t grainIndex) final;
        bool makeProgress() final;
        bool makeProgressBlocking(std::chrono::steady_clock::duration) final;

    private:
        RDMInitiator(std::shared_ptr<Endpoint>);

        /// Returns true if any of the endpoints contained in this initiator have pending work.
        [[nodiscard]]
        bool hasPendingWork() const noexcept;

        /// Block on the completion queue with a timeout.
        void blockOnCQ(std::chrono::steady_clock::duration);

        /// Poll the completion queue and process the events until the queue is empty.
        void pollCQ();

        /// Attempt to consolidate the state
        void consolidateState();
        /// Try to activate any idle endpoints.
        void activateIdleEndpoints();
        /// Evict any dead endpoints that are no longer used.
        void evictDeadEndpoints();

        /// Consume a completion entry
        void consume(Completion);
        void handleCompletionError(Completion::Error); /// Handles a completion error event.
        void handleCompletionData(Completion::Data);   /// Handle a completion data event.

        std::shared_ptr<Endpoint> _endpoint;

        std::vector<LocalRegionGroup> _localRegions;

        std::map<Endpoint::Id, RDMInitiatorEndpoint> _targets;

        size_t pending{0}; // The number of outstanding transfer posted
    };
}
