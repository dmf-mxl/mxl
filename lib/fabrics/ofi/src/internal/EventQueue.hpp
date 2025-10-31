// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <memory>
#include <rdma/fabric.h>
#include <rdma/fi_eq.h>
#include "Event.hpp"
#include "Fabric.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief RAII Wrapper around a libfabric event queue (`fi_eq`)
     */
    class EventQueue : public std::enable_shared_from_this<EventQueue>
    {
    public:
        /** \brief Attributes to configure newly created event queues.
         */
        struct Attributes final
        {
        public:
            /** \brief Creates a configuration with all values set to reasonable defaults.
             */
            static Attributes defaults();

            /** \brief Returns the raw libfabric version of this object
             */
            [[nodiscard]]
            ::fi_eq_attr raw() const noexcept;

        public:
            std::size_t size; /**< Size of the event queue. */
        };

    public:
        /** \brief Create a new event queue associated with the specified fabric.
         */
        static std::shared_ptr<EventQueue> open(std::shared_ptr<Fabric>, Attributes const& attr = Attributes::defaults());

        ~EventQueue();

        // No copying, no moving
        EventQueue(EventQueue const&) = delete;
        void operator=(EventQueue const&) = delete;
        EventQueue(EventQueue&&) = delete;
        EventQueue& operator=(EventQueue&&) = delete;

        /** \brief Returns a raw handle to the event queue.
         */
        ::fid_eq* raw() noexcept;

        /** \brief Returns a raw handle to the event queue.
         */
        [[nodiscard]]
        ::fid_eq const* raw() const noexcept;

        /** \brief Perform a non-blocking read on the event queue. If no event is available in the queue, this
         * function returns std::nullopt
         */
        std::optional<Event> read();

        /** \brief Perform a blocking read on the event queue.
         *
         * This function will block until the specified timeout
         * has elapsed, the process is signalled, or an event is available on the queue.
         * If no event was available after the timeout, the function returns std::nullopt. If the process is signalled
         * while blocking on this function, an exception will be thrown.
         */
        std::optional<Event> readBlocking(std::chrono::steady_clock::duration timeout);

    private:
        /** \brief Releases all underlying resources. This is called from the destructor and the move-assignment operator.
         */
        void close();

        /** \brief Construct and event queue.
         *
         * The completion queue can only exists behind a shared_ptr. Some events need to carry a reference to the
         * completion queue. To prevent the queue from beeing released before all these events have be released, they own
         * a shared_ptr to the queue they have originated from.
         */
        EventQueue(::fid_eq* raw, std::shared_ptr<Fabric> fabric);

        /** \brief Handle the result of a blocking or non-blocking read.
         *
         * This takes a raw event queue entry and generate an Event instance.
         */
        std::optional<Event> handleReadResult(ssize_t, std::uint32_t, ::fi_eq_entry const&);

    private:
        ::fid_eq* _raw;                  /**< Raw resource reference */
        std::shared_ptr<Fabric> _fabric; /**< Pointer to the fabric for which the event queue was created. */
    };
}
