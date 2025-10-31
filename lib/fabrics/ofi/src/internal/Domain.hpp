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

    /** \brief RAII Wrapper around a libfabric domain (`fi_domain`).
     */
    class Domain
    {
    public:
        ~Domain();

        Domain(Domain const&) = delete;
        void operator=(Domain const&) = delete;

        Domain(Domain&&) noexcept;
        Domain& operator=(Domain&&);
        /** \brief Accessor for the underlying `fi_domain` instance.
         */
        ::fid_domain* raw() noexcept;

        /** \brief Accessor for the underlying `fi_domain` instance.
         */
        [[nodiscard]]
        ::fid_domain const* raw() const noexcept;

        /** \brief Open a new domain associated with the specified fabric.
         */
        static std::shared_ptr<Domain> open(std::shared_ptr<Fabric> fabric);

        /** \brief Register a list of memory regions to this domain.
         *
         * The domain will own its own version of the registered memory regions.
         * A user can retrieve associated local and remote regions using `localRegions()` and `remoteRegions()` respectively.
         */
        void registerRegions(std::vector<Region> const& regions, std::uint64_t access);

        /** \brief Get the local regions associated to the registered regions to this domain.
         *
         * If no regions was registered previously, this will
         * return an empty vector.
         */
        [[nodiscard]]
        std::vector<LocalRegion> localRegions() const noexcept;

        /** \brief Get the remote regions associated to the registered regions to this domain.
         *
         * If no regions was registered previously, this will return an
         * empty vector.
         */
        [[nodiscard]]
        std::vector<RemoteRegion> remoteRegions() const noexcept;

        /** \brief When this returns true, a user should use virtual addresses instead of 0 based addresses when refering a remote address
         * registered with this domain.
         */
        [[nodiscard]]
        bool usingVirtualAddresses() const noexcept;

        /** \brief When this returns true, it means the target must post a fi_recv in order to receive immediate data
         */
        [[nodiscard]]
        bool usingRecvBufForCqData() const noexcept;

        /** \brief Accessor for the fabric associated with this domain.
         */
        [[nodiscard]]
        std::shared_ptr<Fabric> fabric() const noexcept;

    private:
        /** \brief Close this domain and release all associated resources.
         */
        void close();

        [[nodiscard]]
        RegisteredRegion registerRegion(Region const& region, std::uint64_t access);

        Domain(::fid_domain*, std::shared_ptr<Fabric>, std::vector<RegisteredRegion>);

    private:
        ::fid_domain* _raw;              /**< libfabric domain instance. */
        std::shared_ptr<Fabric> _fabric; /**< Fabric associated with this domain. */
        std::vector<RegisteredRegion>
            _registeredRegions;          /**< Registered regions associated with this domain. These are used to generate local and remote regions. */
    };
}
