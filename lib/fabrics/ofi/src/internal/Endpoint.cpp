// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Endpoint.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <uuid.h>
#include <bits/types/struct_iovec.h>
#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_rma.h>
#include "internal/Logging.hpp"
#include "Address.hpp"
#include "CompletionQueue.hpp"
#include "Domain.hpp"
#include "EventQueue.hpp"
#include "Exception.hpp"
#include "FIInfo.hpp"
#include "LocalRegion.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    Endpoint::Id Endpoint::randomId() noexcept
    {
        std::uniform_int_distribution<Endpoint::Id> dist;
        std::random_device rd;
        std::mt19937 eng{rd()};

        return dist(eng);
    }

    void* Endpoint::idToContextValue(Id id) noexcept
    {
        return reinterpret_cast<void*>(id);
    }

    Endpoint::Id Endpoint::contextValueToId(void* contextValue) noexcept
    {
        return reinterpret_cast<Endpoint::Id>(contextValue);
    }

    Endpoint::Id Endpoint::idFromFID(::fid_t fid) noexcept
    {
        return contextValueToId(fid->context);
    }

    Endpoint::Id Endpoint::idFromFID(::fid_ep* fid) noexcept
    {
        return idFromFID(&fid->fid);
    }

    Endpoint Endpoint::create(std::shared_ptr<Domain> domain)
    {
        return Endpoint::create(std::move(domain), randomId());
    }

    Endpoint Endpoint::create(std::shared_ptr<Domain> domain, FIInfoView info)
    {
        return Endpoint::create(std::move(domain), randomId(), info);
    }

    Endpoint Endpoint::create(std::shared_ptr<Domain> domain, Id epid, FIInfoView info)
    {
        ::fid_ep* raw;

        fiCall(::fi_endpoint, "Failed to create endpoint", domain->raw(), info.raw(), &raw, idToContextValue(epid));

        // expose the private constructor to std::make_shared inside this function
        struct MakeSharedEnabler : public Endpoint
        {
            MakeSharedEnabler(::fid_ep* raw, FIInfoView info, std::shared_ptr<Domain> domain)
                : Endpoint(raw, info, domain)
            {}
        };

        return {raw, info, std::move(domain)};
    }

    Endpoint Endpoint::create(std::shared_ptr<Domain> domain, Id epid)
    {
        return Endpoint::create(domain, epid, domain->fabric()->info());
    }

    Endpoint::Id Endpoint::id() const noexcept
    {
        return contextValueToId(_raw->fid.context);
    }

    Endpoint::~Endpoint()
    {
        close();
    }

    Endpoint::Endpoint(::fid_ep* raw, FIInfoView info, std::shared_ptr<Domain> domain, std::optional<std::shared_ptr<CompletionQueue>> cq,
        std::optional<std::shared_ptr<EventQueue>> eq, std::optional<std::shared_ptr<AddressVector>> av)
        : _raw(raw)
        , _info(info.owned())
        , _domain(std::move(domain))
        , _cq(std::move(cq))
        , _eq(std::move(eq))
        , _av(std::move(av))
    {
        MXL_INFO("Endpoint {} created", Endpoint::idFromFID(raw));
    }

    void Endpoint::close()
    {
        if (_raw)
        {
            auto id = this->id();

            fiCall(::fi_close, "Failed to close endpoint", &_raw->fid);

            MXL_INFO("Endoint {} closed", id);

            _raw = nullptr;
        }
    }

    Endpoint::Endpoint(Endpoint&& other) noexcept
        : _raw(other._raw)
        , _info(std::move(other._info))
        , _domain(std::move(other._domain))
        , _cq(std::move(other._cq))
        , _eq(std::move(other._eq))
        , _av(std::move(other._av))
    {
        other._raw = nullptr;
    }

    Endpoint& Endpoint::operator=(Endpoint&& other)
    {
        close();

        _raw = other._raw;
        other._raw = nullptr;

        _info = std::move(other._info);
        _domain = std::move(other._domain);
        _eq = std::move(other._eq);
        _cq = std::move(other._cq);
        _av = std::move(other._av);

        return *this;
    }

    void Endpoint::bind(std::shared_ptr<EventQueue> eq)
    {
        fiCall(::fi_ep_bind, "Failed to bind event queue to endpoint", _raw, &eq->raw()->fid, 0);

        _eq = eq;
    }

    void Endpoint::bind(std::shared_ptr<CompletionQueue> cq, std::uint64_t flags)
    {
        fiCall(::fi_ep_bind, "Failed to bind completion queue to endpoint", _raw, &cq->raw()->fid, flags);

        _cq = cq;
    }

    void Endpoint::bind(std::shared_ptr<AddressVector> av)
    {
        fiCall(::fi_ep_bind, "Failed to bind address vector to endpoint", _raw, &av->raw()->fid, 0);

        _av = av;
    }

    void Endpoint::enable()
    {
        fiCall(::fi_enable, "Failed to enable endpoint", _raw);
    }

    void Endpoint::accept()
    {
        uint8_t dummy;
        fiCall(::fi_accept, "Failed to accept connection", _raw, &dummy, sizeof(dummy));
    }

    void Endpoint::connect(FabricAddress const& addr)
    {
        fiCall(::fi_connect, "Failed to connect endpoint", _raw, addr.raw(), nullptr, 0);
    }

    void Endpoint::shutdown()
    {
        fiCall(::fi_shutdown, "Failed to shutdown endpoint", _raw, 0);
    }

    FabricAddress Endpoint::localAddress() const
    {
        return FabricAddress::fromFid(&_raw->fid);
    }

    std::shared_ptr<CompletionQueue> Endpoint::completionQueue() const
    {
        if (!_cq)
        {
            throw Exception::internal("no Completion queue is bound to the endpoint"); // Is this the right throw??
        }

        return *_cq;
    }

    std::shared_ptr<EventQueue> Endpoint::eventQueue() const
    {
        if (!_eq)
        {
            throw Exception::internal("No event queue bound to the endpoint"); // Is this the right throw??
        }

        return *_eq;
    }

    std::shared_ptr<AddressVector> Endpoint::addressVector() const
    {
        if (!_av)
        {
            throw Exception::internal("No address vector bound to the endpoint!");
        }
        return *_av;
    }

    std::pair<std::optional<Completion>, std::optional<Event>> Endpoint::readQueues()
    {
        std::optional<Completion> completion{std::nullopt};
        std::optional<Event> event{std::nullopt};

        if (_cq)
        {
            completion = (*_cq)->read();
        }

        if (_eq)
        {
            event = (*_eq)->read();
        }

        return {completion, event};
    }

    std::pair<std::optional<Completion>, std::optional<Event>> Endpoint::readQueuesBlocking(std::chrono::steady_clock::duration timeout)
    {
        std::optional<Completion> completion{std::nullopt};
        std::optional<Event> event{std::nullopt};

        // No queues, no blocking;
        if (!_eq && !_cq)
        {
            throw Exception::invalidState("No queues bound to endpoint");
        }

        // There is no event queue, so we just block on the completion queue.
        if (!_eq)
        {
            return {(*_cq)->readBlocking(timeout), event};
        }

        auto timeoutMs = std::chrono::duration_cast<std::remove_const_t<decltype(EQReadInterval)>>(timeout);

        // This is the simple case: The user-supplied timeout is less than the minimum EventQueue read interval. So we just read the EventQueue once
        // and then block on the CompletionQueue until the specified timeout.
        if (timeoutMs < EQReadInterval)
        {
            auto event = (*_eq)->read();
            auto completion = (*_cq)->readBlocking(timeout);

            return {completion, event};
        }

        // The time point at which we must return control to the user.
        auto deadline = std::chrono::steady_clock::now() + timeout;

        for (;;)
        {
            // The maximum duration we can block for until we must return control to the caller.
            auto timeUntilDeadline = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());

            // If the time is up, we return control.
            if (timeUntilDeadline <= std::chrono::milliseconds(0))
            {
                return {completion, event};
            }

            // Do a non-blocking read of the event queue first.
            event = (*_eq)->read();
            if (event)
            {
                return {completion, event};
            }

            // And then block on the completion queue until either the minimum duration between event queue reads has elapsed, or the latest point in
            // time at which we need to return control to the caller has been reached.
            completion = (*_cq)->readBlocking(std::min(EQReadInterval, timeUntilDeadline));
            if (completion)
            {
                return {completion, event};
            }
        }
    }

    std::shared_ptr<Domain> Endpoint::domain() const
    {
        return _domain;
    }

    FIInfoView Endpoint::info() const noexcept
    {
        return _info.view();
    }

    ::fid_ep* Endpoint::raw() noexcept
    {
        return _raw;
    }

    ::fid_ep const* Endpoint::raw() const noexcept
    {
        return _raw;
    }

    void Endpoint::write(LocalRegionGroup const& localGroup, RemoteRegionGroup const& remoteGroup, ::fi_addr_t destAddr,
        std::optional<std::uint64_t> immData)
    {
        std::uint64_t data = immData.value_or(0);
        std::uint64_t flags = FI_DELIVERY_COMPLETE;
        flags |= immData.has_value() ? FI_REMOTE_CQ_DATA : 0;

        ::fi_msg_rma msg = {
            .msg_iov = localGroup.asIovec(),
            .desc = const_cast<void**>(localGroup.desc()),
            .iov_count = localGroup.size(),
            .addr = destAddr,
            .rma_iov = remoteGroup.asRmaIovs(),
            .rma_iov_count = remoteGroup.size(),
            .context = _raw,
            .data = data,
        };

        fiCall(::fi_writemsg, "Failed to push rma write to work queue.", _raw, &msg, flags);
    }

    void Endpoint::recv(LocalRegion region)
    {
        auto iovec = region.toIovec();
        fiCall(::fi_recv, "Failed to push recv to work queue", _raw, iovec.iov_base, iovec.iov_len, nullptr, FI_ADDR_UNSPEC, nullptr);
    }
}
