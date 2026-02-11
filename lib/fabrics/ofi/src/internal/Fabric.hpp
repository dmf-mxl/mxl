// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/** \file Fabric.hpp
 * \brief Wrapper for libfabric fabric (fid_fabric) - top-level container for RDMA resources.
 *
 * A Fabric is the top-level resource in libfabric's hierarchy. It represents a single network fabric
 * (a logical or physical network of RDMA-capable devices).
 *
 * Libfabric resource hierarchy:
 * Fabric → Domain → Endpoint/CQ/EQ/AV
 *
 * The Fabric object:
 * - Encapsulates provider-specific implementation (TCP, Verbs, EFA, SHM, etc.)
 * - Created from FabricInfo which specifies provider, addressing, capabilities
 * - Parent for Domain objects (which manage memory registration and resource allocation)
 *
 * Typical workflow:
 * 1. Query available providers with fi_getinfo()
 * 2. Select appropriate FabricInfo
 * 3. Open Fabric with Fabric::open(info)
 * 4. Create Domain from Fabric
 * 5. Create Endpoints, CQs, EQs, AVs from Domain
 */

#pragma once

#include <memory>
#include <rdma/fabric.h>
#include "FabricInfo.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief RAII wrapper around a libfabric fabric object (`fid_fabric`).
     *
     * Fabric is the top-level resource container representing a network fabric instance.
     */
    class Fabric
    {
    public:
        ~Fabric();

        Fabric(Fabric const&) = delete;
        void operator=(Fabric const&) = delete;

        Fabric(Fabric&&) noexcept;
        Fabric& operator=(Fabric&&);

        /** \brief Mutable accessor for the underlying raw libfabric fabric object.
         */
        [[nodiscard]]
        ::fid_fabric* raw() noexcept;
        /** \brief Immutable accessor for the underlying raw libfabric fabric object.
         */
        [[nodiscard]]
        ::fid_fabric const* raw() const noexcept;

        /** \brief Open a fabric object based on the provided FabricInfoView.
         *
         * \param info The fabric info to use when opening the fabric.
         * \return A shared pointer to the opened Fabric object.
         */
        static std::shared_ptr<Fabric> open(FabricInfoView info);

        /** \brief Get a view on the libfabric info used to open this fabric.
         */
        [[nodiscard]]
        FabricInfoView info() const noexcept;

    private:
        void close();

        Fabric(::fid_fabric* raw, FabricInfoView info);

    private:
        ::fid_fabric* _raw;
        FabricInfo _info;
    };

}
