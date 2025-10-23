// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include "Fabric.hpp"
#include "LocalRegion.hpp"
#include "Region.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    class RegisteredRegion;

    class Domain
    {
    public:
        ~Domain();

        Domain(Domain const&) = delete;
        void operator=(Domain const&) = delete;

        Domain(Domain&&) noexcept;
        Domain& operator=(Domain&&);

        ::fid_domain* raw() noexcept;
        [[nodiscard]]
        ::fid_domain const* raw() const noexcept;

        static std::shared_ptr<Domain> open(std::shared_ptr<Fabric> fabric);

        /// Register a list of memory regions to this domain. The domain will own the registered groups.
        void registerRegions(std::vector<Region> const& regions, std::uint64_t access);

        /// Get the local groups associated with the registered groups to this domain.
        [[nodiscard]]
        std::vector<LocalRegion> localRegions() const noexcept;

        /// Get the remote groups associated with the registered groups to this domain.
        [[nodiscard]]
        std::vector<RemoteRegion> remoteRegions() const noexcept;

        [[nodiscard]]
        bool usingVirtualAddresses() const noexcept;

        // When this returns true, it means the target must post a fi_recv in order to receive completions
        [[nodiscard]]
        bool usingRecvBufForCqData() const noexcept;

        [[nodiscard]]
        std::shared_ptr<Fabric> fabric() const noexcept
        {
            return _fabric;
        }

    private:
        void close();

        [[nodiscard]]
        RegisteredRegion registerRegion(Region const& region, std::uint64_t access);

        Domain(::fid_domain*, std::shared_ptr<Fabric>, std::vector<RegisteredRegion>);

    private:
        ::fid_domain* _raw;
        std::shared_ptr<Fabric> _fabric;
        std::vector<RegisteredRegion> _registeredRegions;
    };
}
