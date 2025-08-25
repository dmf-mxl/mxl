// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "EventQueue.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include "internal/Logging.hpp"
#include "Exception.hpp"
#include "Fabric.hpp"

namespace mxl::lib::fabrics::ofi
{

    EventQueueAttr EventQueueAttr::defaults()
    {
        EventQueueAttr attr{};
        attr.size = 8; // default size, this should be parameterized
        return attr;
    }

    ::fi_eq_attr EventQueueAttr::raw() const noexcept
    {
        ::fi_eq_attr raw{};
        raw.size = size;
        raw.wait_obj = FI_WAIT_UNSPEC; // default wait object, this should be parameterized
        raw.wait_set = nullptr;        // only used if wait_obj is FI_WAIT_SET
        raw.flags = 0;                 // if signaling vector is required, this should use the flag "FI_AFFINITY"
        raw.signaling_vector = 0;      // this should indicate the CPU core that interrupts associated with the eq should target.
        return raw;
    }

    std::shared_ptr<EventQueue> EventQueue::open(std::shared_ptr<Fabric> fabric, EventQueueAttr const& attr)
    {
        ::fid_eq* eq;
        auto eq_attr = attr.raw();

        fiCall(::fi_eq_open, "Failed to open event queue", fabric->raw(), &eq_attr, &eq, nullptr);

        // expose the private constructor to std::make_shared inside this function
        struct MakeSharedEnabler : public EventQueue
        {
            MakeSharedEnabler(::fid_eq* raw, std::shared_ptr<Fabric> fabric)
                : EventQueue(raw, fabric)
            {}
        };

        return std::make_shared<MakeSharedEnabler>(eq, fabric);
    }

    std::optional<Event> EventQueue::read()
    {
        uint32_t eventType;
        ::fi_eq_entry entry;

        auto const ret = fi_eq_read(_raw, &eventType, &entry, sizeof(entry), 0);

        return handleReadResult(ret, eventType, entry);
    }

    std::optional<Event> EventQueue::readBlocking(std::chrono::steady_clock::duration timeout)
    {
        auto timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
        if (timeoutMs == 0)
        {
            return read();
        }

        uint32_t eventType;
        ::fi_eq_entry entry;

        auto const ret = fi_eq_sread(_raw, &eventType, &entry, sizeof(entry), timeoutMs, 0);

        return handleReadResult(ret, eventType, entry);
    }

    EventQueue::EventQueue(::fid_eq* raw, std::shared_ptr<Fabric> fabric)
        : _raw(raw)
        , _fabric(std::move(fabric))
    {}

    EventQueue::~EventQueue()
    {
        close();
    }

    ::fid_eq* EventQueue::raw() noexcept
    {
        return _raw;
    }

    ::fid_eq const* EventQueue::raw() const noexcept
    {
        return _raw;
    }

    void EventQueue::close()
    {
        if (_raw)
        {
            fiCall(::fi_close, "Failed to close event queue", &_raw->fid);
            _raw = nullptr;
        }
    }

    std::optional<Event> EventQueue::handleReadResult(ssize_t ret, uint32_t eventType, ::fi_eq_entry const& entry)
    {
        if (ret == -FI_EAGAIN)
        {
            // No event available
            return std::nullopt;
        }

        if (ret == -FI_EAVAIL)
        {
            ::fi_eq_err_entry eq_err{};
            fi_eq_readerr(_raw, &eq_err, 0);

            return Event::fromError(this->shared_from_this(), &eq_err);
        }

        if (ret < 0)
        {
            throw FIException::make(ret, "Failed to read from event queue: {}", ::fi_strerror(ret));
        }

        switch (eventType)
        {
            case FI_CONNECTED:
            case FI_CONNREQ:
            case FI_SHUTDOWN:      return Event::fromRawCMEntry(*reinterpret_cast<fi_eq_cm_entry const*>(&entry), eventType); break;
            case FI_NOTIFY:
            case FI_MR_COMPLETE:
            case FI_AV_COMPLETE:
            case FI_JOIN_COMPLETE: return Event::fromRawEntry(entry, eventType); break;
            default:               MXL_WARN("Unhandled event on event queue: code: {}", eventType); return std::nullopt;
        }
    }
}
