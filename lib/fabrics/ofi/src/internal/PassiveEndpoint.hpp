#pragma once

#include <memory>
#include <optional>
#include <rdma/fabric.h>
#include <rdma/fi_endpoint.h>
#include "Address.hpp"
#include "Event.hpp"
#include "EventQueue.hpp"
#include "Fabric.hpp"

namespace mxl::lib::fabrics::ofi
{
    class PassiveEndpoint
    {
    public:
        ~PassiveEndpoint();

        PassiveEndpoint(PassiveEndpoint const&) = delete;
        void operator=(PassiveEndpoint const&) = delete;

        PassiveEndpoint(PassiveEndpoint&&) noexcept;
        PassiveEndpoint& operator=(PassiveEndpoint&&);

        static PassiveEndpoint create(std::shared_ptr<Fabric>);

        void bind(std::shared_ptr<EventQueue> eq);

        void listen();
        void reject(Event& entry);

        [[nodiscard]]
        std::shared_ptr<EventQueue> eventQueue() const;

        FabricAddress localAddress()
        {
            return FabricAddress::fromFid(&_raw->fid);
        }

        ::fid_pep* raw() noexcept;

        [[nodiscard]]
        ::fid_pep const* raw() const noexcept;

    private:
        void close();

        PassiveEndpoint(::fid_pep* raw, std::shared_ptr<Fabric> fabric, std::optional<std::shared_ptr<EventQueue>> eq = std::nullopt);

        ::fid_pep* _raw;
        std::shared_ptr<Fabric> _fabric;

        std::optional<std::shared_ptr<EventQueue>> _eq;
    };
}
