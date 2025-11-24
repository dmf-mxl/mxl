// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <rdma/fabric.h>
#include "mxl/fabrics.h"
#include "DataLayout.hpp"
#include "Endpoint.hpp"
#include "GrainSlices.hpp"
#include "Initiator.hpp"
#include "LocalRegion.hpp"
#include "Protocol.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief A single endpoint within an RDMInitiator.
     */
    class RDMInitiatorEndpoint
    {
    public:
        /** \brief Construct a new RDMInitiatorEndpoint object.
         *
         * \param ep The endpoint to use for posting transfers.
         * \param addr The fabric address of the remote endpoint.
         * \param regions The remote memory regions to transfer data to.
         */
        RDMInitiatorEndpoint(std::shared_ptr<Endpoint> ep, DataLayout const& layout, TargetInfo info);

        /** \brief Returns true if the endpoint is idle and could be actived.
         */
        [[nodiscard]]
        bool isIdle() const noexcept;

        /** \brief Returns true if the endpoint was shut down and can be evicted from the initiator.
         */
        [[nodiscard]]
        bool canEvict() const noexcept;

        /** \brief Try to activate the endpoint.
         *
         * The endpoint is activated by adding the remote address to the address vector.
         */
        void activate();

        /** \brief Initiate a shutdown process.
         *
         * The endpoint will have pending work until a shutdown or error event will be received. After which it can be evicted from the initiator.
         */
        void shutdown();

        /** \brief Post a data transfer request to this endpoint.
         */
        std::size_t postTransfer(LocalRegion const& localRegion, std::uint64_t remoteIndex, std::uint64_t remotePayloadOffset,
            SliceRange const& sliceRange);

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
            ::fi_addr_t fiAddr;                    /**< Address index in address vector. */
            std::unique_ptr<EgressProtocol> proto; /**< Selected protocol for data transfers. */
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
        State _state;                  /**< The current state of the endpoint. */
        std::shared_ptr<Endpoint> _ep; /** The endpoint used to post transfer with. There is only one endpoint shared for all targets in constrast to
                                        * the RCInitiator where each target will have their own endpoint. */
        DataLayout const&
            _dataLayout;  /**< Data layout inherited by the parent RDMInitiator. RDMInitiatorEndpoint's can't outlive the parent RDMInitiator.*/
        TargetInfo _info; /**< Target information for this endpoint. */
    };

    /** \brief An initiator that uses reliable datagram (RDM) endpoints for data transfers.
     *
     * RDM endpoints provide connectionless communication with reliability guarantees. An address vector is used to manage destination addresses.
     */
    class RDMInitiator : public Initiator
    {
    public:
        static std::unique_ptr<RDMInitiator> setup(mxlInitiatorConfig const&);

        /** \copydoc Initiator::addTarget()
         */
        void addTarget(TargetInfo const&) override;

        /** \copydoc Initiator::removeTarget()
         */
        void removeTarget(TargetInfo const&) override;

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
        RDMInitiator(std::shared_ptr<Endpoint>, DataLayout dataLayout);

        /** \brief Returns true if any of the endpoints contained in this initiator have pending work.
         */
        [[nodiscard]]
        bool hasPendingWork() const noexcept;

        /** \brief Returns true if the initiator has at least 1 target added no matter what the state is.
         */
        [[nodiscard]]
        bool hasTarget() const noexcept;

        /** \brief Block on the completion queue with a timeout.
         */
        void blockOnCQ(std::chrono::steady_clock::duration);

        /** \brief Poll the completion queue and process the events until the queue is empty.
         */
        void pollCQ();

        /** \brief Attempt to consolidate the state
         */
        void consolidateState();

        /** \brief Try to activate any idle endpoints.
         */
        void activateIdleEndpoints();

        /** \brief Evict any dead endpoints that are no longer used.
         */
        void evictDeadEndpoints();

        /** \brief Consume a completion entry
         */
        void consume(Completion);

        /** \brief Handle a completion error event.
         */
        void handleCompletionError(Completion::Error);

        /** \brief Handle a completion data event.
         */
        void handleCompletionData(Completion::Data);

    private:
        std::shared_ptr<Endpoint> _endpoint;    /** Shared endpoint used for all transfers. */

        DataLayout _dataLayout;                 /**< Data layout used for all transfers. */
        std::vector<LocalRegion> _localRegions; /**< Local memory regions registered for data transfers. */

        std::map<Endpoint::Id, RDMInitiatorEndpoint>
            _targets;      /**< Targets managed by this initiator. Each target has its own remote address and remote memory regions. */

        size_t pending{0}; /**< The number of outstanding transfer posted */
    };
}
