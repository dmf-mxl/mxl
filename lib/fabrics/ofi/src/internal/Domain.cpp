// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Domain.hpp"
#include <cstdint>
#include <limits>
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
    namespace
    {
        void validateRegion(Region const& region)
        {
            if (region.registrationSize < region.size)
            {
                throw Exception::invalidArgument(
                    "Region registration size {} is smaller than its logical size {}.", region.registrationSize, region.size);
            }
            if (region.registrationSize > (std::numeric_limits<std::uintptr_t>::max() - region.base))
            {
                throw Exception::invalidArgument(
                    "Region at address {} with registration size {} exceeds the address space.", region.base, region.registrationSize);
            }
        }

        /// Return true if every region resides in host memory and its mapped
        /// registration span ends exactly where the next region begins.
        bool regionsAreContiguousHost(std::vector<Region> const& regions) noexcept
        {
            if (regions.size() < 2)
            {
                return false;
            }

            if (!regions.front().loc.isHost())
            {
                return false;
            }

            for (auto i = std::size_t{1}; i < regions.size(); ++i)
            {
                if (!regions[i].loc.isHost())
                {
                    return false;
                }
                if ((regions[i].base < regions[i - 1].base) || ((regions[i].base - regions[i - 1].base) != regions[i - 1].registrationSize))
                {
                    return false;
                }
            }
            return true;
        }
    }

    Domain::Domain(::fid_domain* raw, std::shared_ptr<Fabric> fabric, std::vector<RegisteredRegion> registeredRegions)
        : _raw(raw)
        , _fabric(std::move(fabric))
        , _registeredRegions(std::move(registeredRegions))
    {}

    Domain::~Domain()
    {
        catchAndLogFabricError([this]() { close(); }, "Failed to close domain");
    }

    std::shared_ptr<Domain> Domain::open(std::shared_ptr<Fabric> fabric)
    {
        ::fid_domain* domain;

        fiCall(::fi_domain2, "Failed to open domain", fabric->raw(), fabric->info().raw(), &domain, 0, nullptr);

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
        for (auto const& region : regions)
        {
            validateRegion(region);
        }

        // When the regions are contiguous in host memory, register them as a single
        // memory region and let each registered region reference it at its own offset.
        // This collapses N registrations (one per grain) into one, which matters for
        // NICs/providers that support only a bounded number of memory regions and for
        // integrations that DMA-map the same memory. The per-region path is used when
        // the regions are not contiguous (e.g. the per-grain file layout).
        if (regionsAreContiguousHost(regions))
        {
            auto const base = regions.front().base;
            auto const finalOffset = regions.back().base - base;
            if (regions.back().registrationSize > (std::numeric_limits<std::size_t>::max() - finalOffset))
            {
                throw Exception::invalidArgument("Contiguous region registration span exceeds the supported size.");
            }
            auto const total = finalOffset + regions.back().registrationSize;

            auto const spanning = Region{base, total, nullptr, nullptr, regions.front().loc};
            auto mr = std::make_shared<MemoryRegion>(MemoryRegion::reg(*this, spanning, access));

            for (auto const& region : regions)
            {
                _registeredRegions.emplace_back(mr, region, region.base - base);
            }
            return;
        }

        std::ranges::transform(
            regions, std::back_inserter(_registeredRegions), [&](auto const& region) { return registerRegionImpl(region, access); });
    }

    void Domain::registerRegion(Region const& region, std::uint64_t access)
    {
        _registeredRegions.push_back(registerRegionImpl(region, access));
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
        return (_fabric->info().raw()->domain_attr->mr_mode & FI_MR_VIRT_ADDR) != 0;
    }

    bool Domain::usingRecvBufForCqData() const noexcept
    {
        return (_fabric->info().raw()->rx_attr->mode & FI_RX_CQ_DATA) != 0;
    }

    std::shared_ptr<Fabric> Domain::fabric() const noexcept
    {
        return _fabric;
    }

    RegisteredRegion Domain::registerRegionImpl(Region const& region, std::uint64_t access)
    {
        validateRegion(region);
        return RegisteredRegion{std::make_shared<MemoryRegion>(MemoryRegion::reg(*this, region, access)), region};
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
