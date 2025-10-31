// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <rdma/fabric.h>
#include "FabricInfo.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief RAIII wrapper around a libfabric fabric object (`fid_fabric`).
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
