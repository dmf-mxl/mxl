// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Notify.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <mxl-internal/Logging.hpp>
#include <rdma/fabric.h>
#include "Exception.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        /** \brief Encode a notify value as notifyMessageSize little endian bytes.
         * \param[in] value The notify value.
         * \return The message payload.
         */
        [[nodiscard]]
        std::array<std::uint8_t, notifyMessageSize> encodeNotifyValue(std::uint32_t value) noexcept
        {
            return {static_cast<std::uint8_t>(value),
                static_cast<std::uint8_t>(value >> 8U),
                static_cast<std::uint8_t>(value >> 16U),
                static_cast<std::uint8_t>(value >> 24U)};
        }

        /** \brief Decode a notify value from notifyMessageSize little endian bytes.
         * \param[in] bytes Pointer to the message payload.
         * \return The notify value.
         */
        [[nodiscard]]
        std::uint32_t decodeNotifyValue(std::uint8_t const* bytes) noexcept
        {
            return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                   (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
        }
    }

    std::optional<NotifyMode> notifyModeFromEnvironment()
    {
        auto const* envValue = std::getenv(notifyModeEnvVar.data());
        if (envValue == nullptr)
        {
            return std::nullopt;
        }

        auto const mode = notifyModeFromString(envValue);
        if (!mode)
        {
            throw Exception::invalidArgument("Invalid value '{}' for {}. Expected 'imm' or 'send'.", envValue, notifyModeEnvVar);
        }
        return mode;
    }

    NotifyMode selectEgressNotifyMode(Domain const& domain, TargetInfo const& remote)
    {
        auto const localMode = domain.notifyMode();
        if ((localMode == NotifyMode::Immediate) && (remote.notifyMode == NotifyMode::Immediate))
        {
            return NotifyMode::Immediate;
        }

        auto const* info = domain.fabric()->info().raw();
        if (!domain.sendAfterWriteOrdered())
        {
            throw Exception::invalidState("Send notifications need the provider to order sends after writes (FI_ORDER_SAW). Provider {} does not.",
                info->fabric_attr->prov_name);
        }
        if (info->tx_attr->inject_size < notifyMessageSize)
        {
            throw Exception::invalidState("Send notifications need an inject size of at least {} bytes. Provider {} supports {}.",
                notifyMessageSize,
                info->fabric_attr->prov_name,
                info->tx_attr->inject_size);
        }

        if (remote.notifyMode == NotifyMode::Immediate)
        {
            MXL_WARN("Using send notifications toward target {}, which advertises immediate data. The target must run an MXL version that "
                     "accepts send notifications, otherwise it never reports the transfers.",
                remote.id);
        }
        else
        {
            MXL_INFO("Using send notifications toward target {}.", remote.id);
        }

        return NotifyMode::Send;
    }

    NotifySender::NotifySender(NotifyMode mode, Completion::Token token) noexcept
        : _mode{mode}
        , _token{token}
    {}

    NotifyMode NotifySender::mode() const noexcept
    {
        return _mode;
    }

    std::optional<std::uint32_t> NotifySender::immData(std::uint32_t value) const noexcept
    {
        if (_mode == NotifyMode::Immediate)
        {
            return value;
        }
        return std::nullopt;
    }

    std::size_t NotifySender::notify(Endpoint const& ep, std::uint32_t value, ::fi_addr_t destAddr)
    {
        if (_mode == NotifyMode::Immediate)
        {
            return 0;
        }

        _destAddr = destAddr;

        // Nothing is waiting, so this notification cannot overtake an older one. Post it without touching the queue.
        if (_queue.empty())
        {
            if (send(ep, value))
            {
                return 1;
            }

            MXL_DEBUG("Transmit queue full, notification waiting.");
            _queue.push_back(value);
            return 0;
        }

        // Queue first, so a notification never overtakes one that is still waiting.
        _queue.push_back(value);
        return flush(ep);
    }

    std::size_t NotifySender::flush(Endpoint const& ep)
    {
        auto posted = std::size_t{0};
        while (!_queue.empty())
        {
            if (!send(ep, _queue.front()))
            {
                MXL_DEBUG("Transmit queue full, {} notification(s) waiting.", _queue.size());
                break;
            }
            _queue.pop_front();
            ++posted;
        }
        return posted;
    }

    bool NotifySender::send(Endpoint const& ep, std::uint32_t value) const
    {
        auto const payload = encodeNotifyValue(value);
        return ep.sendInline(_token, payload.data(), payload.size(), _destAddr);
    }

    bool NotifySender::hasPending() const noexcept
    {
        return !_queue.empty();
    }

    void NotifySender::reset() noexcept
    {
        _queue.clear();
    }

    NotifyReceiver::NotifyReceiver()
        : _slots(maxSlots)
    {}

    void NotifyReceiver::start(Endpoint const& ep)
    {
        if (!_memoryRegion)
        {
            _domain = ep.domain();
            auto const region = Region{reinterpret_cast<std::uintptr_t>(_slots.data()), _slots.size() * sizeof(std::uint64_t), nullptr, nullptr};
            _memoryRegion.emplace(MemoryRegion::reg(*_domain, region, FI_RECV));
        }

        // Every consumed buffer produces a completion, so more buffers than the completion queue holds only invite an overrun. The receive
        // queue bounds the count as well.
        auto const queueSize = std::min(ep.info()->rx_attr->size, ep.completionQueue()->size());
        _postedCount = std::clamp(queueSize, std::size_t{1}, maxSlots);
        _stopped = false;
        _unposted.clear();
        for (auto slot = std::size_t{0}; slot < _postedCount; ++slot)
        {
            post(ep, slot);
        }

        MXL_DEBUG("Posted {} notification receive buffers.", _postedCount);
    }

    std::optional<std::uint32_t> NotifyReceiver::read(Endpoint const& ep, Completion::Data const& completion)
    {
        if (auto const immData = completion.data(); immData)
        {
            // Remote CQ data. With FI_RX_CQ_DATA it used up the posted buffer named in the completion context, without writing to it.
            if (ep.domain()->usingRecvBufForCqData())
            {
                auto const slot = static_cast<std::size_t>(completion.token());
                if (slot < _postedCount)
                {
                    post(ep, slot);
                }
                else
                {
                    MXL_WARN("Remote CQ data consumed unknown notification buffer {}, not posting it again.", slot);
                }
            }

            return static_cast<std::uint32_t>(*immData);
        }

        if (!completion.isRecv())
        {
            return std::nullopt;
        }

        auto const slot = static_cast<std::size_t>(completion.token());
        if (slot >= _postedCount)
        {
            MXL_WARN("Received a message for unknown notification buffer {}, ignoring it.", slot);
            return std::nullopt;
        }

        auto value = std::optional<std::uint32_t>{};
        if (completion.length() >= notifyMessageSize)
        {
            value = decodeNotifyValue(reinterpret_cast<std::uint8_t const*>(&_slots[slot]));
        }
        else
        {
            MXL_WARN("Received a {} byte message, expected a {} byte notification. Ignoring it.", completion.length(), notifyMessageSize);
        }

        // The value is copied out, so the buffer can take the next notification.
        post(ep, slot);
        return value;
    }

    void NotifyReceiver::stop() noexcept
    {
        _stopped = true;
    }

    void NotifyReceiver::post(Endpoint const& ep, std::size_t slot)
    {
        if (_stopped)
        {
            // The connection is closed. Posting on its endpoint would fail.
            return;
        }

        // Buffers that waited for room go first. In the common case none are waiting and the queue is not touched.
        while (!_unposted.empty())
        {
            if (!postSlot(ep, _unposted.front()))
            {
                _unposted.push_back(slot);
                MXL_DEBUG("Receive queue full, {} notification buffer(s) waiting to be posted.", _unposted.size());
                return;
            }
            _unposted.pop_front();
        }

        if (!postSlot(ep, slot))
        {
            _unposted.push_back(slot);
            MXL_DEBUG("Receive queue full, notification buffer {} waiting to be posted.", slot);
        }
    }

    bool NotifyReceiver::postSlot(Endpoint const& ep, std::size_t slot)
    {
        auto const region = LocalRegion{
            .addr = reinterpret_cast<std::uint64_t>(&_slots[slot]),
            .len = sizeof(std::uint64_t),
            .desc = _memoryRegion->desc(),
        };
        return ep.recv(region, static_cast<Completion::Token>(slot));
    }
}
