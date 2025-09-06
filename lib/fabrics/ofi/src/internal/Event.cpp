// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Event.hpp"
#include <rdma/fabric.h>
#include "EventQueue.hpp"
#include "Exception.hpp"
#include "FIInfo.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{
    Event::ConnectionRequested::ConnectionRequested(::fid_t fid, FIInfo info)
        : _fid(fid)
        , _info(std::move(info))
    {}

    ::fid_t Event::ConnectionRequested::fid() const noexcept
    {
        return _fid;
    }

    FIInfoView Event::ConnectionRequested::info() const noexcept
    {
        return _info.view();
    }

    Event::Connected::Connected(::fid_t fid)
        : _fid(fid)
    {}

    ::fid_t Event::Connected::fid() const noexcept
    {
        return _fid;
    }

    Event::Shutdown::Shutdown(::fid_t fid)
        : _fid(fid)
    {}

    ::fid_t Event::Shutdown::fid() const noexcept
    {
        return _fid;
    }

    Event::Error::Error(std::shared_ptr<EventQueue> eq, ::fid_t fid, int err, int providerErr, std::vector<uint8_t> errData)
        : _eq(std::move(eq))
        , _fid(fid)
        , _err(err)
        , _providerErr(providerErr)
        , _errData(std::move(errData))
    {}

    int Event::Error::code() const noexcept
    {
        return _err;
    }

    int Event::Error::providerCode() const noexcept
    {
        return _providerErr;
    }

    ::fid_t Event::Error::fid() const noexcept
    {
        return _fid;
    }

    std::string Event::Error::toString() const
    {
        return ::fi_eq_strerror(_eq->raw(), _providerErr, _errData.data(), nullptr, 0);
    }

    Event::Event(Inner ev)
        : _event(std::move(ev))
    {}

    Event Event::fromRawEntry(::fi_eq_entry const&, uint32_t)
    {
        throw Exception::internal("unimplemented");
    }

    Event Event::fromRawCMEntry(::fi_eq_cm_entry const& entry, uint32_t eventType)
    {
        // clang-format off
        switch (eventType)
        {
            case FI_CONNREQ:   return {ConnectionRequested{entry.fid, FIInfo::own(entry.info)}};
            case FI_CONNECTED: return {Connected{entry.fid}};
            case FI_SHUTDOWN:  return {Shutdown{entry.fid}};
            default:           throw Exception::internal("Unsupported event type returned from queue");
        }
        // clang-format on
    }

    Event Event::fromError(std::shared_ptr<EventQueue> queue, ::fi_eq_err_entry const* raw)
    {
        auto errDataBuffer = reinterpret_cast<uint8_t*>(raw->err_data);

        return {
            Error{
                  std::move(queue),
                  raw->fid,
                  raw->err,
                  raw->prov_errno,
                  std::vector<uint8_t>(errDataBuffer, errDataBuffer + raw->err_data_size),
                  },
        };
    }

    bool Event::isConnReq() const noexcept
    {
        return std::holds_alternative<ConnectionRequested>(_event);
    }

    bool Event::isConnected() const noexcept
    {
        return std::holds_alternative<Connected>(_event);
    }

    bool Event::isShutdown() const noexcept
    {
        return std::holds_alternative<Shutdown>(_event);
    }

    bool Event::isError() const noexcept
    {
        return std::holds_alternative<Error>(_event);
    }

    FIInfoView Event::info() const
    {
        if (auto connReq = std::get_if<ConnectionRequested>(&_event); connReq != nullptr)
        {
            return connReq->info();
        }

        throw Exception::invalidState("Tried to access fi_info from an event that is not a connection request");
    }

    std::string Event::errorString() const
    {
        if (auto err = std::get_if<Error>(&_event); err != nullptr)
        {
            return err->toString();
        }

        throw Exception::invalidState("Tried to access error string from an event that is not an error");
    }

    ::fid_t Event::fid() noexcept
    {
        return std::visit(
            overloaded{
                [](ConnectionRequested& ev) { return ev.fid(); },
                [](Connected& ev) { return ev.fid(); },
                [](Shutdown& ev) { return ev.fid(); },
                [](Error& err) { return err.fid(); },
            },
            _event);
    }
}
