// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

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
    Domain::Domain(::fid_domain* raw, std::shared_ptr<Fabric> fabric, std::vector<RegisteredRegionGroup> registeredRegionGroups)
        : _raw(raw)
        , _fabric(std::move(fabric))
        , _registeredRegionGroups(std::move(registeredRegionGroups))
    {}

    Domain::~Domain()
    {
        close();
    }

    std::shared_ptr<Domain> Domain::open(std::shared_ptr<Fabric> fabric)
    {
        ::fid_domain* domain;

        fiCall(::fi_domain2, "Failed to open domain", fabric->raw(), fabric->info().raw(), &domain, 0, nullptr);

        struct MakeSharedEnabler : public Domain
        {
            MakeSharedEnabler(::fid_domain* domain, std::shared_ptr<Fabric> fabric, std::vector<RegisteredRegionGroup> registerRegionGroups)
                : Domain(domain, std::move(fabric), std::move(registerRegionGroups))
            {}
        };

        return std::make_shared<MakeSharedEnabler>(domain, std::move(fabric), std::vector<RegisteredRegionGroup>{});
    }

    void Domain::registerRegionGroups(RegionGroups const& regionGroups, std::uint64_t access)
    {
        std::ranges::transform(
            regionGroups, std::back_inserter(_registeredRegionGroups), [&](auto const& group) { return registerRegionGroup(group, access); });
    }

    std::vector<LocalRegionGroup> Domain::localRegionGroups() const noexcept
    {
        return toLocal(_registeredRegionGroups);
    }

    std::vector<RemoteRegionGroup> Domain::RemoteRegionGroups() const noexcept
    {
        return toRemote(_registeredRegionGroups, usingVirtualAddresses());
    }

    bool Domain::usingVirtualAddresses() const noexcept
    {
        return (_fabric->info().raw()->domain_attr->mr_mode & FI_MR_VIRT_ADDR) != 0;
    }

    bool Domain::usingRecvBufForCqData() const noexcept
    {
        return (_fabric->info().raw()->rx_attr->mode & FI_RX_CQ_DATA) != 0;
    }

    RegisteredRegion Domain::registerRegion(Region const& region, std::uint64_t access)
    {
        return RegisteredRegion{MemoryRegion::reg(*this, region, access), region};
    }

    RegisteredRegionGroup Domain::registerRegionGroup(RegionGroup const& regionGroup, std::uint64_t access)
    {
        std::vector<RegisteredRegion> out;
        std::ranges::transform(regionGroup, std::back_inserter(out), [&](auto const& region) { return registerRegion(region, access); });
        return RegisteredRegionGroup{std::move(out)};
    }

    void Domain::close()
    {
        _registeredRegionGroups.clear();

        if (_raw != nullptr)
        {
            fiCall(::fi_close, "Failed to close domain", &_raw->fid);
            _raw = nullptr;
        }
    }

    Domain::Domain(Domain&& other) noexcept
        : _raw(other._raw)
        , _fabric(std::move(other._fabric))
        , _registeredRegionGroups(std::move(other._registeredRegionGroups))
    {
        other._raw = nullptr;
    }

    Domain& Domain::operator=(Domain&& other)
    {
        close();

        _raw = other._raw;
        other._raw = nullptr;

        _fabric = std::move(other._fabric);
        _registeredRegionGroups = std::move(other._registeredRegionGroups);

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
