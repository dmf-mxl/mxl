// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Completion.hpp"
#include <optional>
#include <variant>
#include <rdma/fi_eq.h>
#include "CompletionQueue.hpp" // IWYU pragma: keep
#include "Exception.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{
    Completion::Data::Data(::fi_cq_data_entry const& raw)
        : _raw(raw)
    {}

    std::optional<std::uint64_t> Completion::Data::data() const noexcept
    {
        if (!(_raw.flags & FI_REMOTE_CQ_DATA))
        {
            return std::nullopt;
        }

        return _raw.data;
    }

    bool Completion::Data::isRemoteWrite() const noexcept
    {
        return (_raw.flags & FI_RMA) && (_raw.flags & FI_REMOTE_WRITE);
    }

    bool Completion::Data::isLocalWrite() const noexcept
    {
        return (_raw.flags & FI_RMA) && (_raw.flags & FI_WRITE);
    }

    Completion::Error::Error(::fi_cq_err_entry const& raw, std::shared_ptr<CompletionQueue> cq)
        : _raw(raw)
        , _cq(std::move(cq))
    {}

    std::string Completion::Error::toString() const
    {
        return ::fi_cq_strerror(_cq->raw(), _raw.prov_errno, _raw.err_data, nullptr, 0);
    }

    ::fid_ep* Completion::Error::fid() const noexcept
    {
        return reinterpret_cast<::fid_ep*>(_raw.op_context);
    }

    Completion::Data Completion::data() const
    {
        if (auto data = std::get_if<Completion::Data>(&_inner); data)
        {
            return *data;
        }

        throw Exception::invalidState("Failed to unwrap completion queue entry as data entry");
    }

    ::fid_ep* Completion::Data::fid() const noexcept
    {
        return reinterpret_cast<::fid_ep*>(_raw.op_context);
    }

    Completion::Completion(Data entry)
        : _inner(entry)
    {}

    Completion::Completion(Error entry)
        : _inner(entry)
    {}

    Completion::Error Completion::err() const
    {
        if (auto error = std::get_if<Error>(&_inner); error)
        {
            return *error;
        }

        throw Exception::invalidState("Failed to unwrap completion queue entry as error");
    }

    std::optional<Completion::Data> Completion::tryData() const noexcept
    {
        if (auto data = std::get_if<Data>(&_inner); data)
        {
            return {*data};
        }

        return std::nullopt;
    }

    std::optional<Completion::Error> Completion::tryErr() const noexcept
    {
        if (auto error = std::get_if<Error>(&_inner); error)
        {
            return {*error};
        }

        return std::nullopt;
    }

    bool Completion::isDataEntry() const noexcept
    {
        return std::holds_alternative<Data>(_inner);
    }

    bool Completion::isErrEntry() const noexcept
    {
        return std::holds_alternative<Error>(_inner);
    }

    ::fid_ep* Completion::fid() const noexcept
    {
        return std::visit(
            overloaded{
                [](Completion::Data const& data) -> ::fid_ep* { return data.fid(); },
                [](Completion::Error const& err) -> ::fid_ep* { return err.fid(); },
            },
            _inner);
    }
}
