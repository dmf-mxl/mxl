// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/** \file Domain.cpp
 * \brief Implementation of Domain wrapper - handles domain lifecycle and memory registration.
 */

#include "Domain.hpp"
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include <rdma/fabric.h>
#include "Exception.hpp"
#include "Fabric.hpp"
#include "MemoryRegion.hpp"
#include "Region.hpp"
#include "RegisteredRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    Domain::Domain(::fid_domain* raw, std::shared_ptr<Fabric> fabric, std::vector<RegisteredRegion> registeredRegions)
        : _raw(raw)
        , _fabric(std::move(fabric))
        , _registeredRegions(std::move(registeredRegions))
    {}

    Domain::~Domain()
    {
        close();
    }

    std::shared_ptr<Domain> Domain::open(std::shared_ptr<Fabric> fabric)
    {
        ::fid_domain* domain;

        // Open domain using fi_domain2() - creates resource container for this fabric
        fiCall(::fi_domain2, "Failed to open domain", fabric->raw(), fabric->info().raw(), &domain, 0, nullptr);

        // Use MakeSharedEnabler pattern to access private constructor with std::make_shared
        struct MakeSharedEnabler : public Domain
        {
            MakeSharedEnabler(::fid_domain* domain, std::shared_ptr<Fabric> fabric, std::vector<RegisteredRegion> registerRegions)
                : Domain(domain, std::move(fabric), std::move(registerRegions))
            {}
        };

        return std::make_shared<MakeSharedEnabler>(domain, std::move(fabric), std::vector<RegisteredRegion>{});
    }

    void Domain::registerRegions(std::vector<Region> const& regions, std::uint64_t access)
    {
        // Register each Region with fi_mr_reg(), storing RegisteredRegion wrappers
        // Access flags specify permissions (read/write/remote_read/remote_write)
        std::ranges::transform(regions, std::back_inserter(_registeredRegions), [&](auto const& region) { return registerRegion(region, access); });
    }

    std::vector<LocalRegion> Domain::localRegions() const noexcept
    {
        return toLocal(_registeredRegions);
    }

    std::vector<RemoteRegion> Domain::remoteRegions() const noexcept
    {
        return toRemote(_registeredRegions, usingVirtualAddresses());
    }

    bool Domain::usingVirtualAddresses() const noexcept
    {
        // Check if provider requires virtual addresses (actual pointer values) vs offset-based (0-based) addressing
        // FI_MR_VIRT_ADDR means remote peer must use actual virtual addresses when accessing our memory
        return (_fabric->info().raw()->domain_attr->mr_mode & FI_MR_VIRT_ADDR) != 0;
    }

    bool Domain::usingRecvBufForCqData() const noexcept
    {
        // Check if provider requires posted receive buffer to receive immediate data (CQ data)
        // FI_RX_CQ_DATA means target must call fi_recv() to accept immediate data with RDMA writes
        return (_fabric->info().raw()->rx_attr->mode & FI_RX_CQ_DATA) != 0;
    }

    std::shared_ptr<Fabric> Domain::fabric() const noexcept
    {
        return _fabric;
    }

    RegisteredRegion Domain::registerRegion(Region const& region, std::uint64_t access)
    {
        return RegisteredRegion{MemoryRegion::reg(*this, region, access), region};
    }

    void Domain::close()
    {
        _registeredRegions.clear();

        if (_raw != nullptr)
        {
            fiCall(::fi_close, "Failed to close domain", &_raw->fid);
            _raw = nullptr;
        }
    }

    Domain::Domain(Domain&& other) noexcept
        : _raw(other._raw)
        , _fabric(std::move(other._fabric))
        , _registeredRegions(std::move(other._registeredRegions))
    {
        other._raw = nullptr;
    }

    Domain& Domain::operator=(Domain&& other)
    {
        close();

        _raw = other._raw;
        other._raw = nullptr;

        _fabric = std::move(other._fabric);
        _registeredRegions = std::move(other._registeredRegions);

        return *this;
    }

    ::fid_domain* Domain::raw() noexcept
    {
        return _raw;
    }

    ::fid_domain const* Domain::raw() const noexcept
    {
        return _raw;
    }
}
