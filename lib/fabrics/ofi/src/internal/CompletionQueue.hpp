// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <rdma/fi_eq.h>
#include "Completion.hpp"
#include "Domain.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief RAII Wrapper around a libfabric completion queue (`fi_cq`).
     */
    class CompletionQueue : public std::enable_shared_from_this<CompletionQueue>
    {
    public:
        /** \brief A collection of attributes that can be used to configure the
         * operation of newly created completion queues.
         */
        struct Attributes
        {
        public:
            /** \brief Returns the a CompletionQueueAttr with all of its attributes
             * set to reasonable defaults.
             */
            static Attributes defaults();

            /** \brief Returns the raw libfabric version of this object.
             */
            [[nodiscard]]
            ::fi_cq_attr raw() const noexcept;

        public:
            std::size_t size;            /**< Size of the queue. */
            enum fi_wait_obj waitObject; /**< The underlying wait object that should be used. */
        };

    public:
        /** \brief Create a new completion queue in the specified domain.
         */
        static std::shared_ptr<CompletionQueue> open(std::shared_ptr<Domain> domain, Attributes const& attr = Attributes::defaults());

        ~CompletionQueue();

        // No copying, no moving
        CompletionQueue(CompletionQueue const&) = delete;
        void operator=(CompletionQueue const&) = delete;
        CompletionQueue(CompletionQueue&&) = delete;
        CompletionQueue& operator=(CompletionQueue&&) = delete;

        /** \brief Mutable accessor of the underlying `fi_cq` instance.
         */
        ::fid_cq* raw() noexcept;

        /** \brief Immutable accessor of the underlying `fi_cq` instance.
         */
        [[nodiscard]]
        ::fid_cq const* raw() const noexcept;

        /** \brief Perform a non-blocking read on the completion queue.
         *
         * If no completion event is available in the queue
         * returns std::nullopt
         */
        std::optional<Completion> read();

        /** \brief Perform a blocking read on the completion queue.
         *
         * This function will block until the specified timeout
         * has elapsed, the process is signaled, or a completion is available on the queue.
         * If no event was available after the timeout, the function returns std::nullopt. If the process is signaled
         * while blocking on this functions, an exception will be thrown.
         */
        std::optional<Completion> readBlocking(std::chrono::steady_clock::duration timeout);

    private:
        /** \brief Releases all underlying resources. This is called from the destructor and the move-assignment operator.
         */
        void close();

        /** \brief The completion queue can only exists behind a shared_ptr.
         *
         * Some events need to carry a reference to the
         * completion queue. To prevent the queue from being released before all these events have be released, they own
         * a shared_ptr to the queue they have originated from.
         */
        CompletionQueue(::fid_cq* raw, std::shared_ptr<Domain> domain);

        /** \brief Handle the result of a blocking or non-blocking read.
         *
         * This takes a raw completion entry and generate a Completion instance.
         */
        std::optional<Completion> handleReadResult(ssize_t ret, ::fi_cq_data_entry const& entry);

    private:
        ::fid_cq* _raw;                  /**< Raw resource reference. */
        std::shared_ptr<Domain> _domain; /**< The domain this queue was created into. */
    };
}
