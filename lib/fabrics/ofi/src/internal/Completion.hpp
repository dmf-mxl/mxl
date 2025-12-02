// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <rdma/fi_eq.h>

namespace mxl::lib::fabrics::ofi
{
    class CompletionQueue;

    /** \brief Type to represent a completion entry from a CompletionQueue
     */
    class Completion
    {
    public:
        using Token = std::uintptr_t;

        /** \brief Data variant of the completion entry instance
         */
        class Data
        {
        public:
            /** \brief Accessor for the immediate data associated with the completion entry
             */
            [[nodiscard]]
            std::optional<std::uint64_t> data() const noexcept;

            /** \brief This used to associate a completion entry with an endpoint when multiple endpoints use the same completion queue.
             */
            [[nodiscard]]
            Completion::Token token() const noexcept;

            /** \brief Indicates whether the completion entry represents a remote write operation.
             */
            [[nodiscard]]
            bool isRemoteWrite() const noexcept;

            /** \brief Indicates whether the completion entry represents a local write operation.
             */
            [[nodiscard]]
            bool isLocalWrite() const noexcept;

        private:
            friend class CompletionQueue;

        private:
            explicit Data(::fi_cq_data_entry const& raw);

        private:
            ::fi_cq_data_entry _raw;
        };

        /** \brief Error variant of the completion entry instance
         */

        class Error
        {
        public:
            /** \brief Generate a hman-readable string representation of the error entry
             */
            [[nodiscard]]
            std::string toString() const;

            /** \brief This used to associate a completion entry with an endpoint when multiple endpoints use the same completion queue.
             */
            [[nodiscard]]
            Token token() const noexcept;

        private:
            friend class CompletionQueue;

        private:
            explicit Error(::fi_cq_err_entry const& raw, std::shared_ptr<CompletionQueue> queue);

        private:
            ::fi_cq_err_entry _raw;
            std::shared_ptr<CompletionQueue> _cq;
        };

    public:
        explicit Completion(Data entry);
        explicit Completion(Error entry);

        static Token randomToken();

        constexpr static void* tokenToContextValue(Token token)
        {
            return std::bit_cast<void*>(token);
        }

        constexpr static Token tokenFromContextValue(void* contextValue)
        {
            return std::bit_cast<Token>(contextValue);
        }

        /** \brief Accessor for the data variant. Calling this on an error entry will throw
         */
        [[nodiscard]]
        Data data() const;

        /** \brief Accessor for the error variant. Calling this on a data entry will throw
         */
        [[nodiscard]]
        Error err() const;

        /** \brief Attempt to access the data variant. Returns std::nullopt if this is an error entry
         */
        [[nodiscard]]
        std::optional<Data> tryData() const noexcept;

        /** \brief Attempt to access the error variant. Returns std::nullopt if this is a data entry
         */
        [[nodiscard]]
        std::optional<Error> tryErr() const noexcept;

        /** \brief Check whether this is a data entry
         */
        [[nodiscard]]
        bool isDataEntry() const noexcept;

        /** \brief check whether this is an error entry
         */
        [[nodiscard]]
        bool isErrEntry() const noexcept;

        /** \brief Get the token that was associated with the operation that
         * has completed.
         */
        [[nodiscard]]
        Token token() const noexcept;

    private:
        friend class CompletionQueue;

        using Inner = std::variant<Data, Error>;

    private:
        Inner _inner;
    };
}
