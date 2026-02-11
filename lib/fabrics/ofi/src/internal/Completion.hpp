// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Completion.hpp
 * @brief Completion entries from the CompletionQueue - indicates RDMA operation finished
 *
 * WHAT IS A COMPLETION?
 * In RDMA/libfabric, operations (writes, reads, sends) are asynchronous. When you call
 * fi_write() to initiate an RDMA write, it returns immediately. Later, when the operation
 * actually completes (data has been sent and acknowledged), a "completion" entry appears
 * in the CompletionQueue.
 *
 * COMPLETION TYPES:
 * - **Data**: Normal successful completion - contains flags, optional immediate data, endpoint FID
 * - **Error**: Operation failed - contains error code, provider-specific error details
 *
 * USAGE PATTERN:
 * 1. Initiator calls endpoint.write() to enqueue an RDMA write
 * 2. Later, initiator polls completionQueue.read() or readBlocking()
 * 3. When a completion arrives, check isDataEntry() vs isErrEntry()
 * 4. For data entries: check isRemoteWrite() to identify the type of operation
 * 5. For error entries: call err().toString() to get human-readable error description
 *
 * IMMEDIATE DATA:
 * RDMA writes can optionally carry 4 bytes of "immediate data" delivered with the completion.
 * In MXL, this is used to send the grain index along with the RDMA write, so the target
 * knows which grain was just received without inspecting the payload.
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <rdma/fi_eq.h>

namespace mxl::lib::fabrics::ofi
{
    class CompletionQueue;

    /**
     * @class Completion
     * @brief Type-safe wrapper around a libfabric completion queue entry
     *
     * Represents either a successful completion (Data) or a failed operation (Error).
     * Uses std::variant for type safety - you must check which type before accessing.
     */
    class Completion
    {
    public:
        /**
         * @class Data
         * @brief Successful completion entry containing operation results
         *
         * A Data entry indicates that an RDMA operation (write, read, send) completed successfully.
         * It contains:
         * - Flags indicating the operation type (RMA write, RMA read, etc.)
         * - Optional immediate data (user-provided metadata sent with the operation)
         * - Endpoint FID to identify which endpoint generated this completion
         */
        class Data
        {
        public:
            /**
             * @brief Get the immediate data (if any) sent with this operation
             *
             * @return std::optional<uint64_t> The 64-bit immediate data, or std::nullopt if none was sent
             *
             * Immediate data is a small (64-bit) piece of metadata that can be sent along with
             * an RDMA write. It's delivered directly in the completion without DMA overhead.
             * In MXL, this is typically used to send the grain index so the target knows which
             * buffer was just written to.
             *
             * The data is only present if the FI_REMOTE_CQ_DATA flag is set in the completion.
             */
            [[nodiscard]]
            std::optional<std::uint64_t> data() const noexcept;

            /**
             * @brief Get the endpoint FID that generated this completion
             *
             * @return ::fid_ep* Pointer to the endpoint's libfabric handle
             *
             * When multiple endpoints share the same completion queue, this identifies
             * which endpoint generated this particular completion. The FID comes from
             * the op_context field that was set when the operation was initiated.
             */
            [[nodiscard]]
            ::fid_ep* fid() const noexcept;

            /**
             * @brief Check if this completion is for a remote write operation
             *
             * @return true if this is a completion for an RDMA write initiated by a remote peer
             *
             * This flag is set when the local endpoint received an RDMA write from a remote
             * initiator. It indicates the FI_RMA and FI_REMOTE_WRITE flags are set.
             */
            [[nodiscard]]
            bool isRemoteWrite() const noexcept;

            /**
             * @brief Check if this completion is for a local write operation
             *
             * @return true if this is a completion for an RDMA write initiated locally
             *
             * This flag is set when the local endpoint completed an RDMA write to a remote target.
             * It indicates the FI_RMA and FI_WRITE flags are set.
             */
            [[nodiscard]]
            bool isLocalWrite() const noexcept;

        private:
            friend class CompletionQueue; ///< CompletionQueue constructs Data objects from raw completions

        private:
            /**
             * @brief Private constructor from raw libfabric completion entry
             * @param raw The fi_cq_data_entry from libfabric
             */
            explicit Data(::fi_cq_data_entry const& raw);

        private:
            ::fi_cq_data_entry _raw; ///< Raw libfabric completion entry (includes flags, data, op_context)
        };

        /**
         * @class Error
         * @brief Failed completion entry containing error information
         *
         * An Error entry indicates that an RDMA operation failed. It contains:
         * - Generic error code (FI_EAGAIN, FI_ENOMEM, etc.)
         * - Provider-specific error code
         * - Provider-specific error data (opaque binary blob)
         * - Endpoint FID to identify which endpoint generated the error
         */
        class Error
        {
        public:
            /**
             * @brief Generate a human-readable string describing the error
             *
             * @return std::string Error description from the provider
             *
             * This calls fi_cq_strerror() to get a provider-specific error message.
             * The message includes both the generic libfabric error code and provider-specific
             * details (e.g., "EFA: remote QP not found" or "Verbs: send queue full").
             */
            [[nodiscard]]
            std::string toString() const;

            /**
             * @brief Get the endpoint FID that generated this error
             *
             * @return ::fid_ep* Pointer to the endpoint's libfabric handle
             *
             * Identifies which endpoint experienced the error. Useful when multiple
             * endpoints share the same completion queue.
             */
            [[nodiscard]]
            ::fid_ep* fid() const noexcept;

        private:
            friend class CompletionQueue; ///< CompletionQueue constructs Error objects from raw error entries

        private:
            /**
             * @brief Private constructor from raw libfabric error entry
             * @param raw The fi_cq_err_entry from libfabric
             * @param queue Shared pointer to the completion queue (needed for fi_cq_strerror)
             */
            explicit Error(::fi_cq_err_entry const& raw, std::shared_ptr<CompletionQueue> queue);

        private:
            ::fi_cq_err_entry _raw;          ///< Raw libfabric error entry (includes err, prov_errno, err_data)
            std::shared_ptr<CompletionQueue> _cq; ///< The queue this error came from (needed for error string conversion)
        };

    public:
        /**
         * @brief Construct a Completion from a successful Data entry
         * @param entry The Data completion to wrap
         */
        explicit Completion(Data entry);

        /**
         * @brief Construct a Completion from a failed Error entry
         * @param entry The Error completion to wrap
         */
        explicit Completion(Error entry);

        /**
         * @brief Access the data variant (throws if this is an error entry)
         *
         * @return Data The successful completion data
         * @throws Exception if this Completion actually contains an Error
         *
         * Use isDataEntry() to check first, or use tryData() for safe access.
         */
        [[nodiscard]]
        Data data() const;

        /**
         * @brief Access the error variant (throws if this is a data entry)
         *
         * @return Error The error completion data
         * @throws Exception if this Completion actually contains Data
         *
         * Use isErrEntry() to check first, or use tryErr() for safe access.
         */
        [[nodiscard]]
        Error err() const;

        /**
         * @brief Safely attempt to access the data variant
         *
         * @return std::optional<Data> The data if this is a data entry, std::nullopt if error
         *
         * This is the safe way to access the data variant without exceptions.
         */
        [[nodiscard]]
        std::optional<Data> tryData() const noexcept;

        /**
         * @brief Safely attempt to access the error variant
         *
         * @return std::optional<Error> The error if this is an error entry, std::nullopt if data
         *
         * This is the safe way to access the error variant without exceptions.
         */
        [[nodiscard]]
        std::optional<Error> tryErr() const noexcept;

        /**
         * @brief Check if this is a successful data entry
         *
         * @return true if this Completion contains a Data object
         */
        [[nodiscard]]
        bool isDataEntry() const noexcept;

        /**
         * @brief Check if this is an error entry
         *
         * @return true if this Completion contains an Error object
         */
        [[nodiscard]]
        bool isErrEntry() const noexcept;

        /**
         * @brief Get the endpoint FID associated with this completion
         *
         * @return ::fid_ep* The endpoint that generated this completion (works for both Data and Error)
         *
         * This is a convenience method that works regardless of whether this is a Data or Error.
         * It uses std::visit to extract the FID from whichever variant is active.
         */
        [[nodiscard]]
        ::fid_ep* fid() const noexcept;

    private:
        friend class CompletionQueue; ///< CompletionQueue constructs Completion objects

        using Inner = std::variant<Data, Error>; ///< Type-safe union of Data or Error

    private:
        Inner _inner; ///< The actual completion data (either Data or Error)
    };
}
