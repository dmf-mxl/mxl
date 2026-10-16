// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>
#include <rdma/fabric.h>
#include "Completion.hpp"
#include "Domain.hpp"
#include "Endpoint.hpp"
#include "MemoryRegion.hpp"
#include "NotifyMode.hpp"
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief Size in bytes of a send notification. The payload is the notify value as a little endian 32 bit integer.
     */
    constexpr auto notifyMessageSize = sizeof(std::uint32_t);

    /** \brief Choose the notify mode an initiator uses toward one target.
     *
     * Send is used when either side needs it: the target advertises Send, or the local domain cannot send immediate data. Send requires the
     * provider to order sends after earlier writes (FI_ORDER_SAW) and to inject at least notifyMessageSize bytes.
     * \param[in] domain The initiator's domain.
     * \param[in] remote The target's info.
     * \return The notify mode to use for transfers to this target.
     * \throws Exception if Send is needed but the provider lacks FI_ORDER_SAW or a large enough inject size.
     */
    [[nodiscard]]
    NotifyMode selectEgressNotifyMode(Domain const& domain, TargetInfo const& remote);

    /** \brief Posts the notifications of an egress protocol in Send notify mode.
     *
     * When the transmit queue is full, notifications are kept in order and posted by later calls to notify() or flush(). A notification is never
     * dropped, because the target would not report the grain or samples it describes. Each posted notification produces one completion with
     * the protocol's token, so the protocol counts it as pending until it was delivered, like a write.
     */
    class NotifySender
    {
    public:
        /** \brief Create a sender for the given notify mode.
         * \param[in] mode The notify mode chosen for the target.
         * \param[in] token Token of the egress protocol. Reported in the completion of each notification.
         */
        NotifySender(NotifyMode mode, Completion::Token token) noexcept;

        /** \brief The notify mode of this sender.
         * \return The notify mode passed to the constructor.
         */
        [[nodiscard]]
        NotifyMode mode() const noexcept;

        /** \brief The immediate data to attach to the last write of a transfer.
         * \param[in] value The notify value of the transfer.
         * \return The value in Immediate mode, std::nullopt in Send mode.
         */
        [[nodiscard]]
        std::optional<std::uint32_t> immData(std::uint32_t value) const noexcept;

        /** \brief Notify the target after all writes of a transfer were posted. Does nothing in Immediate mode.
         * \param[in] ep The endpoint the writes were posted on.
         * \param[in] value The notify value of the transfer.
         * \param[in] destAddr The target address. Ignored for connection-oriented endpoints.
         * \return The number of notifications posted, each of which produces one completion. Can include notifications kept from earlier calls.
         */
        [[nodiscard]]
        std::size_t notify(Endpoint const& ep, std::uint32_t value, ::fi_addr_t destAddr);

        /** \brief Post notifications that were kept because the transmit queue was full.
         * \param[in] ep The endpoint used for the transfers.
         * \return The number of notifications posted, each of which produces one completion.
         */
        [[nodiscard]]
        std::size_t flush(Endpoint const& ep);

        /** \brief Whether notifications are waiting to be posted.
         * \return true if at least one notification is waiting.
         */
        [[nodiscard]]
        bool hasPending() const noexcept;

        /** \brief Drop the waiting notifications. Used when the connection to the target is closed.
         */
        void reset() noexcept;

    private:
        /// Returns false if the transmit queue is full.
        [[nodiscard]]
        bool send(Endpoint const& ep, std::uint32_t value) const;

        NotifyMode _mode;                      ///< Notify mode chosen for the target.
        Completion::Token _token;              ///< Token reported in the completions of the notifications.
        std::deque<std::uint32_t> _queue;      ///< Notifications not yet posted, oldest first.
        ::fi_addr_t _destAddr{FI_ADDR_UNSPEC}; ///< Target address of the queued notifications.
    };

    /** \brief Receive buffers for notifications on the target side of an ingress protocol.
     *
     * The buffers serve two purposes. In Send mode they receive the notification messages. With providers that consume a receive buffer for
     * remote CQ data (FI_RX_CQ_DATA mode), the immediate data of a write uses one of them. The same receiver handles both, so a target accepts
     * initiators in either notify mode.
     */
    class NotifyReceiver
    {
    public:
        /** \brief Upper bound for the number of posted buffers. The provider's receive queue size can lower it.
         */
        constexpr static std::size_t maxSlots = 1024;

        /** \brief Allocate the buffers. Registration happens in start().
         */
        NotifyReceiver();

        /** \brief Post the buffers on a newly connected or enabled endpoint.
         *
         * The buffers are registered with the endpoint's domain on the first call. Each call posts as many buffers as both the endpoint's
         * receive queue and its completion queue hold, up to maxSlots. The completion queue depth is the cqDepth setup option of the target,
         * so a target that serves several senders needs a deeper queue.
         * \param[in] ep The endpoint to post the buffers on.
         */
        void start(Endpoint const& ep);

        /** \brief Stop posting buffers again after reading a completion. Used when the connection is closed, while the remaining completions
         * are read. The next call to start() posts the buffers again.
         */
        void stop() noexcept;

        /** \brief Extract the notify value from a completion and post the consumed buffer again.
         * \param[in] ep The endpoint the completion was read from.
         * \param[in] completion The completion entry.
         * \return The notify value, or std::nullopt if the completion carries none.
         */
        [[nodiscard]]
        std::optional<std::uint32_t> read(Endpoint const& ep, Completion::Data const& completion);

    private:
        /** \brief Post one buffer, after the buffers that are still waiting to be posted.
         *
         * A provider can report a full receive queue right after a completion, before it has released the entry of the consumed buffer. The
         * buffer then waits and is posted by a later call.
         * \param[in] ep The endpoint to post the buffer on.
         * \param[in] slot Index of the buffer. Used as the completion context to find the buffer again.
         */
        void post(Endpoint const& ep, std::size_t slot);

        /// Returns false if the receive queue is full.
        [[nodiscard]]
        bool postSlot(Endpoint const& ep, std::size_t slot);

        std::vector<std::uint64_t> _slots;         ///< The buffers. 8 bytes each, of which a notification uses the first notifyMessageSize.
        std::shared_ptr<Domain> _domain;           ///< Domain the buffers are registered with. Kept alive until _memoryRegion is closed.
        std::optional<MemoryRegion> _memoryRegion; ///< Registration of _slots. Declared after _domain so it is closed first.
        std::size_t _postedCount{0};               ///< Number of buffers posted per connection. Slots [0, _postedCount) are in use.
        bool _stopped{false};                      ///< Set by stop(): consumed buffers are not posted again.
        std::deque<std::size_t> _unposted;         ///< Buffers that could not be posted because the receive queue was full, oldest first.
    };
}
