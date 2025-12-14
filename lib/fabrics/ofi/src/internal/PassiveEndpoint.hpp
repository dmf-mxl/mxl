// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <rdma/fi_endpoint.h>
#include "Address.hpp"
#include "Endpoint.hpp"
#include "Event.hpp"
#include "EventQueue.hpp"
#include "Fabric.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief RAII Wrapper around a libfabric passive endpoint (`fid_pep`).
     *
     * A passive endpoint can be viewed like a bound TCP socket listening for incoming connections.
     */
    class PassiveEndpoint
    {
    public:
        ~PassiveEndpoint();

        // Copy constructor and assignment operators are deleted
        PassiveEndpoint(PassiveEndpoint const&) = delete;
        void operator=(PassiveEndpoint const&) = delete;

        // Implement move constructor and assignment operator
        PassiveEndpoint(PassiveEndpoint&&) noexcept;
        PassiveEndpoint& operator=(PassiveEndpoint&&);

        /** \brief Allocate a new PassiveEndpoint associated with the given Fabric.
         *
         * \param fabric The Fabric to create the PassiveEndpoint on.
         * \return A new PassiveEndpoint object.
         */
        [[nodiscard]]
        static PassiveEndpoint create(std::shared_ptr<Fabric> fabric);

        /** \brief Allocate a new PassiveEndpoint associated with the given Fabric.
         *
         * \param fabric The Fabric to create the PassiveEndpoint on.
         * \return A new PassiveEndpoint object.
         */
        [[nodiscard]]
        static PassiveEndpoint create(std::shared_ptr<Fabric> fabric, Endpoint::Id id);

        /** \brief Get the endpoints id.
         */
        [[nodiscard]]
        Endpoint::Id id() const noexcept;

        /** \brief Bind the endpoint to an event queue.
         *
         * The endpoint can be bound to an event queue only once. The Endpoint object will take ownership of
         * an instance of the shared pointer, so the queue can not be destroyed before the endpoint.
         *
         * \param eq The event queue to bind to the endpoint.
         */
        void bind(std::shared_ptr<EventQueue> eq);

        /** \brief Put the passive endpoint into listening mode.
         */
        void listen();

        /** \brief Reject a connection request event.
         *
         * \param entry The event entry representing the connection request to reject.
         */
        void reject(Event& entry);

        /** \brief Accessor for the associated event queue.
         *
         * If an event queue is associated with this endpoint. Get the queue. Throws an exception if no event queue is
         * associated with this endpoint.
         */
        [[nodiscard]]
        std::shared_ptr<EventQueue> eventQueue() const;

        /** \brief Obtain the local fabric address for this passive endpoint.
         */
        [[nodiscard]]
        FabricAddress localAddress()
        {
            return FabricAddress::fromFid(&_raw->fid);
        }

        /** \brief Access the underlying raw `fid_pep` pointer.
         */
        [[nodiscard]]
        ::fid_pep* raw() noexcept;

        /** \copydoc raw()
         */
        [[nodiscard]]
        ::fid_pep const* raw() const noexcept;

    private:
        /** \brief Releases all underlying resources. This is called from the destructor and the move-assignment operator.
         */
        void close();
        /** \brief Construct the PassiveEndpoint.
         *
         * \param raw The raw libfabric fid_pep pointer.
         * \param fabric The Fabric the passive endpoint is associated with.
         * \param eq Optional event queue associated with the passive endpoint.
         */
        PassiveEndpoint(::fid_pep* raw, std::shared_ptr<Fabric> fabric, Endpoint::Id id,
            std::optional<std::shared_ptr<EventQueue>> eq = std::nullopt);

    private:
        ::fid_pep* _raw;                 /**< Raw resource reference */
        std::shared_ptr<Fabric> _fabric; /**< Pointer to the fabric for which the passive endpoint was created. */
        Endpoint::Id _id;

        std::optional<std::shared_ptr<EventQueue>> _eq; /**< Event queue lives here after PassiveEndpoint::bind() */
    };
}
