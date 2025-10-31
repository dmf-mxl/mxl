// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <rdma/fi_eq.h>
#include "FabricInfo.hpp"
#include "variant"

namespace mxl::lib::fabrics::ofi
{
    class EventQueue;

    /** \brief Type to represent an event entry from an EventQueue
     */
    class Event
    {
    public:
        /** \brief A type that wraps a "FI_CONNREQ" event type
         */
        class ConnectionRequested final
        {
        public:
            ConnectionRequested(::fid_t fid, FabricInfo info);

            /** \brief fabric descriptor associated with the event
             */
            [[nodiscard]]
            ::fid_t fid() const noexcept;

            /** \brief libfabric info struct of the endpoint that requested connection
             */
            [[nodiscard]]
            FabricInfoView info() const noexcept;

        private:
            ::fid_t _fid;
            FabricInfo _info;
        };

        /** \brief A type that wraps a "FI_CONNECTED" event type
         */
        class Connected final
        {
        public:
            Connected(::fid_t fid);

            /** \brief fabric descriptor associated with the event
             */
            [[nodiscard]]
            ::fid_t fid() const noexcept;

        private:
            ::fid_t _fid;
        };

        /** \brief A type that wraps a "FI_SHUTDOWN" event type
         */
        class Shutdown final
        {
        public:
            Shutdown(::fid_t fid);

            /** \brief fabric descriptor associated with the event
             */
            [[nodiscard]]
            ::fid_t fid() const noexcept;

        private:
            ::fid_t _fid;
        };

        /** \brief A type that wraps an error received from the event queue.
         */
        class Error final
        {
        public:
            Error(std::shared_ptr<EventQueue> eq, ::fid_t fid, int err, int providerErr, std::vector<std::uint8_t> errData);

            /** \brief return the libfabric error code.
             */
            [[nodiscard]]
            int code() const noexcept;

            /** \brief return the provider-specific error code.
             */
            [[nodiscard]]
            int providerCode() const noexcept;

            /** \brief fabric descriptor associated with the event
             */
            [[nodiscard]]
            ::fid_t fid() const noexcept;

            /** \brief Convert the error to a human-readable string.
             */
            [[nodiscard]]
            std::string toString() const;

        private:
            std::shared_ptr<EventQueue> _eq;
            ::fid_t _fid;
            int _err;
            int _providerErr;
            std::vector<std::uint8_t> _errData;
        };

        using Inner = std::variant<ConnectionRequested, Connected, Shutdown, Error>;

    public:
        static Event fromRawEntry(::fi_eq_entry const& raw, std::uint32_t eventType);

        /** \brief create an Event from a libfabric Connection Management event
         */
        static Event fromRawCMEntry(::fi_eq_cm_entry const& raw, std::uint32_t eventType);

        /** \brief create an Event from a libfabric event error entry
         */
        static Event fromError(std::shared_ptr<EventQueue> queue, ::fi_eq_err_entry const* raw);

        /** \brief returns true if this event is a FI_CONNREQ
         */
        [[nodiscard]]
        bool isConnReq() const noexcept;

        /** \brief returns true if this event is a FI_CONNECTED
         */
        [[nodiscard]]
        bool isConnected() const noexcept;

        /** \brief returns true if this event is a FI_SHUTDOWN
         */
        [[nodiscard]]
        bool isShutdown() const noexcept;

        /** \brief returns true if this event is an error entry
         */
        [[nodiscard]]
        bool isError() const noexcept;

        /** \brief fabric descriptor associated with the event
         */
        [[nodiscard]]
        fid_t fid() noexcept;

    private:
        Event(Inner);

    private:
        Inner _event;
    };
}
