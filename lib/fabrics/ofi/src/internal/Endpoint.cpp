// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Endpoint.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <utility>
#include <uuid.h>
#include <sys/uio.h>
#include <mxl-internal/Logging.hpp>
#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_rma.h>
#include "Address.hpp"
#include "CompletionQueue.hpp"
#include "Domain.hpp"
#include "EventQueue.hpp"
#include "Exception.hpp"
#include "FabricInfo.hpp"
#include "LocalRegion.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    Endpoint::Id Endpoint::randomId() noexcept
    {
        // Use a global static random engine to prevent endpoint id collisions.
        static auto mu = std::mutex{};
        static auto engine = std::mt19937_64{std::random_device{}()};
        auto const lock = std::scoped_lock{mu};
        return std::uniform_int_distribution<Endpoint::Id>{}(engine);
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

    Endpoint Endpoint::create(std::shared_ptr<Domain> domain, FabricInfoView info)
    {
        return Endpoint::create(std::move(domain), randomId(), info);
    }

    Endpoint Endpoint::create(std::shared_ptr<Domain> domain, Id epid, FabricInfoView info)
    {
        ::fid_ep* raw;

        fiCall(::fi_endpoint, "Failed to create endpoint", domain->raw(), info.raw(), &raw, idToContextValue(epid));

        // expose the private constructor to std::make_shared inside this function
        struct MakeSharedEnabler : public Endpoint
        {
            MakeSharedEnabler(::fid_ep* raw, FabricInfoView info, std::shared_ptr<Domain> domain)
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
        catchAndLogFabricError([this]() { close(); }, "Failed to close endpoint");
    }

    Endpoint::Endpoint(::fid_ep* raw, FabricInfoView info, std::shared_ptr<Domain> domain, std::optional<std::shared_ptr<CompletionQueue>> cq,
        std::optional<std::shared_ptr<EventQueue>> eq, std::optional<std::shared_ptr<AddressVector>> av)
        : _raw(raw)
        , _info(info.owned())
        , _domain(std::move(domain))
        , _cq(std::move(cq))
        , _eq(std::move(eq))
        , _av(std::move(av))
    {
        MXL_DEBUG("Endpoint {} created", Endpoint::idFromFID(raw));
    }

    void Endpoint::close()
    {
        if (_raw)
        {
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
            auto id = this->id();
#endif

            fiCall(::fi_close, "Failed to close endpoint", &_raw->fid);

            MXL_DEBUG("Endoint {} closed", id);

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
        auto dummy = std::uint8_t{};
        fiCall(::fi_accept, "Failed to accept connection", _raw, &dummy, sizeof(dummy));
    }

    void Endpoint::connect(RawFabricAddress const& addr)
    {
        fiCall(::fi_connect, "Failed to connect endpoint", _raw, addr.raw(), nullptr, 0);
    }

    void Endpoint::shutdown()
    {
        fiCall(::fi_shutdown, "Failed to shutdown endpoint", _raw, 0);
    }

    RawFabricAddress Endpoint::localAddress() const
    {
        return RawFabricAddress::fromFid(&_raw->fid, _info.view());
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

    FabricInfoView Endpoint::info() const noexcept
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

    void Endpoint::writeImpl(Completion::Token token, ::iovec const* msgIov, std::size_t iovCount, void** desc, ::fi_rma_iov const* rmaIov,
        ::fi_addr_t destAddr, std::optional<std::uint32_t> immData) const
    {
        std::uint64_t data = immData.value_or(0);
        std::uint64_t flags = FI_DELIVERY_COMPLETE;
        flags |= immData.has_value() ? FI_REMOTE_CQ_DATA : 0;

        ::fi_msg_rma msg = {
            .msg_iov = msgIov,
            .desc = desc,
            .iov_count = iovCount,
            .addr = destAddr,
            .rma_iov = rmaIov,
            .rma_iov_count = 1,
            .context = Completion::tokenToContextValue(token),
            .data = data,
        };

        fiCall(::fi_writemsg, "Failed to push rma write to work queue.", _raw, &msg, flags);
    }

    std::size_t Endpoint::write(Completion::Token token, LocalRegion const& local, RemoteRegion const& remote, ::fi_addr_t destAddr,
        std::optional<std::uint32_t> immData) const
    {
        std::vector<void*> descs{local.desc};

        auto msgIov = local.toIovec();
        auto rmaIov = remote.toRmaIov();

        writeImpl(token, &msgIov, 1, descs.data(), &rmaIov, destAddr, immData);

        return 1;
    }

    std::size_t Endpoint::write(Completion::Token token, LocalRegionGroup const& localGroup, RemoteRegion const& remote, ::fi_addr_t destAddr,
        std::optional<std::uint32_t> immData) const
    {
        auto remoteRmaIov = remote.toRmaIov();
        auto remoteOffset = std::size_t{0};

        auto const iovLimit = info().txIovLimit();
        auto const nbWrites = (localGroup.size() + iovLimit - 1) / iovLimit; // ceil(group.size() / iovLimit)

        for (auto i = std::size_t{0}; i < nbWrites; i++)
        {
            auto const begin = i * iovLimit;
            auto const end = begin + std::min(iovLimit, localGroup.size() - begin);

            auto const localGroupSpan = localGroup.span(begin, end);

            // Validate that we don't bust the remote region
            if ((remoteOffset + localGroupSpan.byteSize()) > remote.len)
            {
                throw Exception::invalidArgument("Local region group is too large for the remote region.");
            }

            remoteRmaIov.addr = remote.addr + remoteOffset;
            remoteRmaIov.len = localGroupSpan.byteSize();

            auto const isLastWrite = (i == nbWrites - 1);
            auto const immDataForWrite = isLastWrite ? immData : std::nullopt;

            writeImpl(token,
                localGroupSpan.asIovec(),
                localGroupSpan.size(),
                const_cast<void**>(localGroupSpan.desc()),
                &remoteRmaIov,
                destAddr,
                immDataForWrite);

            remoteOffset += localGroupSpan.byteSize();
        }

        return nbWrites;
    }

    void Endpoint::recv(LocalRegion region) const
    {
        auto iovec = region.toIovec();
        fiCall(::fi_recv, "Failed to push recv to work queue", _raw, iovec.iov_base, iovec.iov_len, nullptr, FI_ADDR_UNSPEC, nullptr);
    }
}
