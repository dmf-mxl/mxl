// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file CompletionQueue.hpp
 * @brief Completion Queue (CQ) wrapper - holds completions for finished RDMA operations
 *
 * WHAT IS A COMPLETION QUEUE?
 * In libfabric/RDMA, operations are asynchronous. When you initiate an RDMA write with fi_write(),
 * the function returns immediately. Later, when the operation actually finishes, a "completion"
 * entry is placed into the Completion Queue.
 *
 * The application must poll or wait on the CQ to retrieve these completions and know when
 * operations have finished.
 *
 * COMPLETION QUEUE VS EVENT QUEUE:
 * - **CompletionQueue (CQ)**: For data path operations (write, read, send, recv completions)
 * - **EventQueue (EQ)**: For control path events (connection established, connection closed, errors)
 *
 * READING COMPLETIONS:
 * - read(): Non-blocking poll - returns immediately with completion or nullopt
 * - readBlocking(timeout): Blocks until completion arrives or timeout expires
 *
 * QUEUE FORMATS:
 * MXL uses FI_CQ_FORMAT_DATA which provides:
 * - Operation flags (FI_RMA, FI_WRITE, FI_REMOTE_WRITE, etc.)
 * - Optional immediate data (64-bit user metadata sent with the operation)
 * - Endpoint context (op_context field identifies the endpoint)
 *
 * USAGE IN MXL:
 * - Initiator: Polls CQ to know when RDMA writes have completed
 * - Target: Polls CQ to know when incoming RDMA writes have arrived
 * - Each endpoint is bound to a CQ before being enabled
 */

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <rdma/fi_eq.h>
#include "Completion.hpp"
#include "Domain.hpp"

namespace mxl::lib::fabrics::ofi
{

    /**
     * @class CompletionQueue
     * @brief RAII wrapper around a libfabric completion queue (fi_cq)
     *
     * Manages the lifecycle of a completion queue and provides methods to read completions.
     * Inherits from enable_shared_from_this because Completion::Error needs to hold a
     * shared_ptr to the CQ for fi_cq_strerror() calls.
     */
    class CompletionQueue : public std::enable_shared_from_this<CompletionQueue>
    {
    public:
        /**
         * @struct Attributes
         * @brief Configuration attributes for creating a CompletionQueue
         */
        struct Attributes
        {
        public:
            /**
             * @brief Get default attributes for a typical completion queue
             * @return Attributes with size=8 and waitObject=FI_WAIT_UNSPEC
             */
            static Attributes defaults();

            /**
             * @brief Convert to raw libfabric fi_cq_attr structure
             * @return ::fi_cq_attr Populated with format=FI_CQ_FORMAT_DATA
             */
            [[nodiscard]]
            ::fi_cq_attr raw() const noexcept;

        public:
            std::size_t size;            ///< Queue depth - number of completion entries it can hold
            enum fi_wait_obj waitObject; ///< Wait mechanism: FI_WAIT_UNSPEC, FI_WAIT_FD, etc.
        };

    public:
        /**
         * @brief Factory method to create and open a completion queue
         * @param domain The domain to create the CQ in
         * @param attr Configuration attributes (defaults are usually fine)
         * @return std::shared_ptr<CompletionQueue> Newly created CQ
         * @throws FabricException if fi_cq_open() fails
         */
        static std::shared_ptr<CompletionQueue> open(std::shared_ptr<Domain> domain, Attributes const& attr = Attributes::defaults());

        /**
         * @brief Destructor - closes the CQ and releases resources
         */
        ~CompletionQueue();

        // No copying or moving - CQ must stay at same address for shared_from_this to work
        CompletionQueue(CompletionQueue const&) = delete;
        void operator=(CompletionQueue const&) = delete;
        CompletionQueue(CompletionQueue&&) = delete;
        CompletionQueue& operator=(CompletionQueue&&) = delete;

        /**
         * @brief Get mutable access to the underlying fi_cq pointer
         * @return ::fid_cq* Raw libfabric completion queue handle
         */
        ::fid_cq* raw() noexcept;

        /**
         * @brief Get const access to the underlying fi_cq pointer
         * @return ::fid_cq const* Raw libfabric completion queue handle
         */
        [[nodiscard]]
        ::fid_cq const* raw() const noexcept;

        /**
         * @brief Non-blocking read of the completion queue
         * @return std::optional<Completion> The completion if available, or nullopt if queue is empty
         *
         * Polls the CQ once using fi_cq_read(). Returns immediately - does not block.
         * Use this in a polling loop for lowest latency.
         */
        std::optional<Completion> read();

        /**
         * @brief Blocking read of the completion queue with timeout
         * @param timeout Maximum time to wait for a completion
         * @return std::optional<Completion> The completion if received, or nullopt if timeout
         * @throws Exception if interrupted by a signal
         *
         * Uses fi_cq_sread() to block until a completion arrives or timeout expires.
         * More CPU-efficient than polling read() in a loop, but slightly higher latency.
         */
        std::optional<Completion> readBlocking(std::chrono::steady_clock::duration timeout);

    private:
        /**
         * @brief Internal helper to close the CQ and release resources
         *
         * Called by destructor. Safe to call multiple times (checks if _raw is non-null).
         */
        void close();

        /**
         * @brief Private constructor - use open() factory method
         * @param raw The raw fi_cq pointer from fi_cq_open()
         * @param domain The domain this CQ belongs to (keeps domain alive)
         *
         * Private because CompletionQueue must only exist behind a shared_ptr
         * (Completion::Error objects hold shared_ptr<CompletionQueue> for fi_cq_strerror).
         */
        CompletionQueue(::fid_cq* raw, std::shared_ptr<Domain> domain);

        /**
         * @brief Common handler for read() and readBlocking() results
         * @param ret Return value from fi_cq_read() or fi_cq_sread()
         * @param entry The raw completion entry structure
         * @return std::optional<Completion> Parsed completion or nullopt
         *
         * Handles three cases:
         * - ret == -FI_EAGAIN: No completion available (returns nullopt)
         * - ret == -FI_EAVAIL: Error completion available (reads error with fi_cq_readerr)
         * - ret > 0: Success (wraps entry in Completion::Data)
         */
        std::optional<Completion> handleReadResult(ssize_t ret, ::fi_cq_data_entry const& entry);

    private:
        ::fid_cq* _raw;                  ///< Raw libfabric CQ handle (or nullptr if closed)
        std::shared_ptr<Domain> _domain; ///< The domain this CQ was created in (keeps domain alive)
    };
}
