// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Completion.cpp
 * @brief Implementation of Completion wrapper classes for libfabric completion queue entries
 *
 * This file provides the implementation for accessing and interpreting completion queue entries,
 * including successful completions (Data) and errors (Error).
 */

#include "Completion.hpp"
#include <optional>
#include <variant>
#include <rdma/fi_eq.h>
#include "CompletionQueue.hpp" // IWYU pragma: keep
#include "Exception.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{
    // Construct Data from raw libfabric completion entry
    Completion::Data::Data(::fi_cq_data_entry const& raw)
        : _raw(raw)
    {}

    // Extract immediate data if present in the completion
    std::optional<std::uint64_t> Completion::Data::data() const noexcept
    {
        // Check if the FI_REMOTE_CQ_DATA flag is set (indicates immediate data was sent)
        if (!(_raw.flags & FI_REMOTE_CQ_DATA))
        {
            return std::nullopt; // No immediate data was sent with this operation
        }

        // Return the immediate data from the completion entry
        return _raw.data;
    }

    // Check if this is a completion for an RDMA write received from a remote initiator
    bool Completion::Data::isRemoteWrite() const noexcept
    {
        // FI_RMA = this is an RMA operation
        // FI_REMOTE_WRITE = the remote side initiated a write to our memory
        return (_raw.flags & FI_RMA) && (_raw.flags & FI_REMOTE_WRITE);
    }

    // Check if this is a completion for an RDMA write we initiated to a remote target
    bool Completion::Data::isLocalWrite() const noexcept
    {
        // FI_RMA = this is an RMA operation
        // FI_WRITE = we initiated a write to remote memory
        return (_raw.flags & FI_RMA) && (_raw.flags & FI_WRITE);
    }

    // Construct Error from raw libfabric error entry
    Completion::Error::Error(::fi_cq_err_entry const& raw, std::shared_ptr<CompletionQueue> cq)
        : _raw(raw)
        , _cq(std::move(cq)) // Keep the queue alive so we can call fi_cq_strerror later
    {}

    // Get a human-readable error string from the provider
    std::string Completion::Error::toString() const
    {
        // Call fi_cq_strerror to get provider-specific error description
        // This combines the generic error code with provider-specific details
        return ::fi_cq_strerror(_cq->raw(), _raw.prov_errno, _raw.err_data, nullptr, 0);
    }

    // Get the endpoint FID from the error entry
    ::fid_ep* Completion::Error::fid() const noexcept
    {
        // The op_context field contains the endpoint pointer
        return reinterpret_cast<::fid_ep*>(_raw.op_context);
    }

    // Access the data variant (throws if this is an error)
    Completion::Data Completion::data() const
    {
        // Try to extract the Data variant from the std::variant
        if (auto data = std::get_if<Completion::Data>(&_inner); data)
        {
            return *data;
        }

        // This is an Error, not Data - throw an exception
        throw Exception::invalidState("Failed to unwrap completion queue entry as data entry.");
    }

    // Get the endpoint FID from the data entry
    ::fid_ep* Completion::Data::fid() const noexcept
    {
        // The op_context field contains the endpoint pointer
        return reinterpret_cast<::fid_ep*>(_raw.op_context);
    }

    // Construct a Completion from a Data entry
    Completion::Completion(Data entry)
        : _inner(entry)
    {}

    // Construct a Completion from an Error entry
    Completion::Completion(Error entry)
        : _inner(entry)
    {}

    // Access the error variant (throws if this is data)
    Completion::Error Completion::err() const
    {
        // Try to extract the Error variant from the std::variant
        if (auto error = std::get_if<Error>(&_inner); error)
        {
            return *error;
        }

        // This is Data, not Error - throw an exception
        throw Exception::invalidState("Failed to unwrap completion queue entry as error.");
    }

    // Safely try to access the data variant (returns nullopt if error)
    std::optional<Completion::Data> Completion::tryData() const noexcept
    {
        if (auto data = std::get_if<Data>(&_inner); data)
        {
            return {*data};
        }

        return std::nullopt;
    }

    // Safely try to access the error variant (returns nullopt if data)
    std::optional<Completion::Error> Completion::tryErr() const noexcept
    {
        if (auto error = std::get_if<Error>(&_inner); error)
        {
            return {*error};
        }

        return std::nullopt;
    }

    // Check if this completion is a Data entry
    bool Completion::isDataEntry() const noexcept
    {
        return std::holds_alternative<Data>(_inner);
    }

    // Check if this completion is an Error entry
    bool Completion::isErrEntry() const noexcept
    {
        return std::holds_alternative<Error>(_inner);
    }

    // Get the endpoint FID (works for both Data and Error)
    ::fid_ep* Completion::fid() const noexcept
    {
        // Use std::visit to call the appropriate fid() method on whichever variant is active
        // overloaded is a helper template from VariantUtils.hpp that allows inline lambda visitors
        return std::visit(
            overloaded{
                [](Completion::Data const& data) -> ::fid_ep* { return data.fid(); },
                [](Completion::Error const& err) -> ::fid_ep* { return err.fid(); },
            },
            _inner);
    }
}
