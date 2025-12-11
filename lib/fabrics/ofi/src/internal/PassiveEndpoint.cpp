// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "PassiveEndpoint.hpp"
#include <mxl-internal/Logging.hpp>
#include <rdma/fi_cm.h>
#include <rdma/fi_endpoint.h>
#include "EventQueue.hpp"
#include "Exception.hpp"
#include "Fabric.hpp"

namespace mxl::lib::fabrics::ofi
{
    PassiveEndpoint PassiveEndpoint::create(std::shared_ptr<Fabric> fabric, Endpoint::Id id)
    {
        ::fid_pep* pep;

        fiCall(::fi_passive_ep, "Failed to create passive endpoint", fabric->raw(), fabric->info().raw(), &pep, nullptr);

        return {pep, fabric, id, std::nullopt};
    }

    PassiveEndpoint PassiveEndpoint::create(std::shared_ptr<Fabric> fabric)
    {
        return PassiveEndpoint::create(std::move(fabric), Endpoint::randomId());
    }

    PassiveEndpoint::~PassiveEndpoint()
    {
        catchErrorAndLog([this]() { close(); }, "Failed to close passive endpoint");
    }

    PassiveEndpoint::PassiveEndpoint(::fid_pep* raw, std::shared_ptr<Fabric> fabric, Endpoint::Id id, std::optional<std::shared_ptr<EventQueue>> eq)
        : _raw(raw)
        , _fabric(std::move(fabric))
        , _id(id)
        , _eq(std::move(eq))
    {}

    Endpoint::Id PassiveEndpoint::id() const noexcept
    {
        return _id;
    }

    void PassiveEndpoint::close()
    {
        if (_raw)
        {
            fiCall(::fi_close, "Failed to close passive endpoint", &_raw->fid);
            _raw = nullptr;
        }
    }

    PassiveEndpoint::PassiveEndpoint(PassiveEndpoint&& other) noexcept
        : _raw(other._raw)
        , _fabric(std::move(other._fabric))
        , _id(other._id)
        , _eq(std::move(other._eq))
    {
        other._raw = nullptr;
    }

    PassiveEndpoint& PassiveEndpoint::operator=(PassiveEndpoint&& other)
    {
        close();

        _raw = other._raw;
        other._raw = nullptr;

        _id = std::move(other._id);
        _fabric = std::move(other._fabric);
        _eq = std::move(other._eq);

        return *this;
    }

    void PassiveEndpoint::bind(std::shared_ptr<EventQueue> eq)
    {
        fiCall(::fi_pep_bind, "Failed to bind event queue to passive endpoint", _raw, &eq->raw()->fid, 0);

        _eq = eq;
    }

    void PassiveEndpoint::listen()
    {
        fiCall(::fi_listen, "Failed to transition the endpoint into listener mode", _raw);
    }

    void PassiveEndpoint::reject(Event& entry)
    {
        auto fid = entry.fid();
        if (!fid)
        {
            MXL_ERROR("Cannot reject a connection notification entry without a fid");
            return;
        }

        fiCall(::fi_reject, "Failed to reject connection request", _raw, fid, nullptr, 0);
    }

    std::shared_ptr<EventQueue> PassiveEndpoint::eventQueue() const
    {
        if (!_eq)
        {
            throw Exception::internal("No event queue bound to this endpoint");
        }

        return *_eq;
    }

    ::fid_pep* PassiveEndpoint::raw() noexcept
    {
        return _raw;
    }

    ::fid_pep const* PassiveEndpoint::raw() const noexcept
    {
        return _raw;
    }

}
