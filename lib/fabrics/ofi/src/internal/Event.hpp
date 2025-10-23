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

    class Event
    {
    public:
        class ConnectionRequested final
        {
        public:
            ConnectionRequested(::fid_t fid, FabricInfo info);

            [[nodiscard]]
            ::fid_t fid() const noexcept;

            [[nodiscard]]
            FabricInfoView info() const noexcept;

        private:
            ::fid_t _fid;
            FabricInfo _info;
        };

        class Connected final
        {
        public:
            Connected(::fid_t fid);

            [[nodiscard]]
            ::fid_t fid() const noexcept;

        private:
            ::fid_t _fid;
        };

        class Shutdown final
        {
        public:
            Shutdown(::fid_t fid);

            [[nodiscard]]
            ::fid_t fid() const noexcept;

        private:
            ::fid_t _fid;
        };

        class Error final
        {
        public:
            Error(std::shared_ptr<EventQueue> eq, ::fid_t fid, int err, int providerErr, std::vector<std::uint8_t> errData);

            [[nodiscard]]
            int code() const noexcept;
            [[nodiscard]]
            int providerCode() const noexcept;

            [[nodiscard]]
            ::fid_t fid() const noexcept;

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
        static Event fromRawCMEntry(::fi_eq_cm_entry const& raw, std::uint32_t eventType);
        static Event fromError(std::shared_ptr<EventQueue> queue, ::fi_eq_err_entry const* raw);

        [[nodiscard]]
        bool isConnReq() const noexcept;

        [[nodiscard]]
        bool isConnected() const noexcept;

        [[nodiscard]]
        bool isShutdown() const noexcept;

        [[nodiscard]]
        bool isError() const noexcept;

        [[nodiscard]]
        FabricInfoView info() const;

        [[nodiscard]]
        std::string errorString() const;

        [[nodiscard]]
        fid_t fid() noexcept;

    private:
        Event(Inner);

    private:
        Inner _event;
    };
}
